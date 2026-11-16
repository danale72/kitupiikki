/*
   Copyright (C) 2026 Alexei Danilov

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QPixmap>

#include "db/kitsasinterface.h"
#include "liite/cacheliite.h"
#include "liite/liitecache.h"

// Crash 2026-10-07, Kitsas_PG, opening a Postgres book from the start page:
//   AloitusSivu::postgresAvaaValittu
//   PostgresModel::avaa            (postgresmodel.cpp:424, yhteysAvattu(nullptr))
//   Kirjanpito::yhteysAvattu       (emits tietokantaVaihtui)
//   LiiteCache::tyhjenna           (liitecache.cpp:183, delete)
//   CacheLiite::~CacheLiite        (QByteArray deref of address 0x153, SIGSEGV)
//
// tyhjenna() walked the hash and deleted every unlocked CacheLiite. The same
// object can sit under two ids (a save response applied twice), so the second
// delete runs the QByteArray destructor on freed memory.

class TyhjaKitsas : public KitsasInterface
{
};

class TestattavaLiiteCache : public LiiteCache
{
public:
    using LiiteCache::LiiteCache;
    using LiiteCache::liiteSaapuu;
    using LiiteCache::tositeSaapuu;

    void asetaRajaKoko(qsizetype raja) { rajaKoko_ = raja; }
    qsizetype koko() const { return koko_; }
    CacheLiite* vanhin() const { return vanhin_; }
    CacheLiite* uusin() const { return uusin_; }
};

class LiiteCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void tyhjenna_samaLiiteKahdellaTunnisteella();
    void tyhjenna_lukittuLiiteSailyy();
    void poistettuLiite_eiKorruptoiValimuistia();
    void tyhjenna_nollaaKoon();
    void kokoRaja_hylkaaVanhimmanJaPalauttaaSen();
    void tyhjenna_lukittuEiJataSeuraajaa();
    void tallennettuNollaTunnisteella_eiJaaValimuistiin();
    void liiteHakematta_samaOlioIlmanHakua();
    void binaariLiite_tallentuuSellaisenaan();
    void tositeValimuisti_otetaanKerran();
    void tositeValimuisti_tallennusUnohtaaSamanEranTositteet();
    void tositeValimuisti_kokoRajoitettu();
    void thumb_lasketaanKerranEikaPdfsta();

private:
    static CacheLiite* tallennettuLiite(const QByteArray& data);
    static CacheLiite* lataa(TestattavaLiiteCache& cache, int id, const QByteArray& data);
    static void tarkistaLista(CacheLiite* vanhin, CacheLiite* uusin, const QList<CacheLiite*>& odotettu);
};

CacheLiite* LiiteCacheTest::tallennettuLiite(const QByteArray& data)
{
    CacheLiite* liite = new CacheLiite();
    liite->setData(data);
    liite->lukitse();
    return liite;
}

CacheLiite* LiiteCacheTest::lataa(TestattavaLiiteCache& cache, int id, const QByteArray& data)
{
    CacheLiite* liite = cache.liite(id);
    QVariant vastaus(data);
    cache.liiteSaapuu(id, &vastaus);
    return liite;
}

void LiiteCacheTest::tarkistaLista(CacheLiite* vanhin, CacheLiite* uusin, const QList<CacheLiite*>& odotettu)
{
    QVERIFY(vanhin);
    QVERIFY(uusin);

    QList<CacheLiite*> eteen;
    for( CacheLiite* liite = vanhin; liite; liite = liite->seuraava()) {
        eteen.append(liite);
        if( liite->seuraava())
            QCOMPARE(liite->seuraava()->edellinen(), liite);
    }
    QCOMPARE(eteen, odotettu);
    QCOMPARE(eteen.last(), uusin);

    QList<CacheLiite*> taakse;
    for( CacheLiite* liite = uusin; liite; liite = liite->edellinen()) {
        taakse.prepend(liite);
        if( liite->edellinen())
            QCOMPARE(liite->edellinen()->seuraava(), liite);
    }
    QCOMPARE(taakse, odotettu);
}

void LiiteCacheTest::tyhjenna_samaLiiteKahdellaTunnisteella()
{
    TyhjaKitsas kitsas;
    LiiteCache cache(nullptr, &kitsas);

    const QByteArray pdf("%PDF-1.4 liite");
    CacheLiite* liite = tallennettuLiite(pdf);

    // Liite::tallennettu / liitetty called twice, server handed out two ids.
    cache.lisaaTallennettu(10, liite);
    cache.lisaaTallennettu(11, liite);
    liite->vapauta();

    // PostgresModel::avaa → yhteysAvattu(nullptr) → tietokantaVaihtui → tyhjenna()
    cache.tyhjenna();
    cache.tyhjenna();

    CacheLiite* uudelleen = cache.liite(10);
    QVERIFY(uudelleen);
    QCOMPARE(uudelleen->tila(), CacheLiite::ALUSTAMATON);
    QVERIFY(uudelleen->data().isEmpty());
}

void LiiteCacheTest::tyhjenna_lukittuLiiteSailyy()
{
    TyhjaKitsas kitsas;
    LiiteCache cache(nullptr, &kitsas);

    const QByteArray pdf("%PDF-1.4 avoin tosite");
    CacheLiite* liite = tallennettuLiite(pdf);
    cache.lisaaTallennettu(7, liite);

    cache.tyhjenna();

    QCOMPARE(liite->data(), pdf);
    QVERIFY(liite->lukossa());
    QCOMPARE(liite->tila(), CacheLiite::KELVOTON);

    liite->vapauta();
    QVERIFY(liite->data().isEmpty());
    delete liite;
}

void LiiteCacheTest::poistettuLiite_eiKorruptoiValimuistia()
{
    TyhjaKitsas kitsas;
    LiiteCache cache(nullptr, &kitsas);

    const QByteArray pdfA("%PDF-1.4 aaa");
    const QByteArray pdfB("%PDF-1.4 bbb-pidempi-sisalto");
    CacheLiite* a = tallennettuLiite(pdfA);
    CacheLiite* b = tallennettuLiite(pdfB);
    cache.lisaaTallennettu(1, a);
    cache.lisaaTallennettu(2, b);

    // LiitteetModel::poista → Liite::poista → ~Liite deletes the cache object.
    cache.poistaPoistettu(2);
    b->vapauta();
    delete b;

    CacheLiite* c = cache.liite(3);
    QVERIFY(c);
    QCOMPARE(a->data(), pdfA);

    a->vapauta();
    cache.tyhjenna();
}

void LiiteCacheTest::tyhjenna_nollaaKoon()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    QVariant data(QByteArray("%PDF-1.4 vanhan kirjanpidon liite"));
    cache.liite(5);
    cache.liiteSaapuu(5, &data);
    QCOMPARE(cache.koko(), data.toByteArray().size());

    cache.tyhjenna();
    QCOMPARE(cache.koko(), qsizetype(0));
}

void LiiteCacheTest::kokoRaja_hylkaaVanhimmanJaPalauttaaSen()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);
    cache.asetaRajaKoko(30);

    const QByteArray aitoA(12, 'a');
    const QByteArray aitoB(12, 'b');
    const QByteArray aitoC(12, 'c');
    CacheLiite* a = lataa(cache, 1, aitoA);
    CacheLiite* b = lataa(cache, 2, aitoB);
    CacheLiite* c = lataa(cache, 3, aitoC);

    // 3×12 exceeds 30, so the oldest attachment is dropped from the list
    // but kept in the hash with its bytes cleared.
    QCOMPARE(a->tila(), CacheLiite::TYHJENNETTY);
    QVERIFY(a->data().isEmpty());
    QCOMPARE(a->seuraava(), nullptr);
    QCOMPARE(a->edellinen(), nullptr);
    QCOMPARE(cache.koko(), aitoB.size() + aitoC.size());
    tarkistaLista(cache.vanhin(), cache.uusin(), {b, c});

    // Move b to the front, then open the evicted attachment again. A stale
    // seuraava on a would clear b's back-link and split the list.
    cache.liite(2);
    cache.liite(1);

    QCOMPARE(cache.koko(), aitoB.size() + aitoC.size());
    QCOMPARE(a->tila(), CacheLiite::ALUSTAMATON);
    QVERIFY(a->data().isEmpty());
    QCOMPARE(b->data(), aitoB);
    QCOMPARE(c->data(), aitoC);
    tarkistaLista(cache.vanhin(), cache.uusin(), {c, b, a});
}

void LiiteCacheTest::tyhjenna_lukittuEiJataSeuraajaa()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    const QByteArray aitoA(12, 'a');
    const QByteArray aitoB(12, 'b');
    CacheLiite* a = lataa(cache, 1, aitoA);
    lataa(cache, 2, aitoB);
    a->lukitse();

    cache.tyhjenna();

    QCOMPARE(a->seuraava(), nullptr);
    QCOMPARE(a->edellinen(), nullptr);
    QCOMPARE(cache.vanhin(), nullptr);
    QCOMPARE(cache.uusin(), nullptr);
    QCOMPARE(cache.koko(), qsizetype(0));
    QCOMPARE(a->tila(), CacheLiite::KELVOTON);
    QCOMPARE(a->data(), aitoA);

    a->vapauta();
    delete a;
}

void LiiteCacheTest::tallennettuNollaTunnisteella_eiJaaValimuistiin()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    // Liite::tallennettu with an id the response didn't carry: Liite keeps
    // ownership and frees the object in ~Liite.
    CacheLiite* liite = tallennettuLiite(QByteArray("%PDF-1.4 tallennettu"));
    cache.lisaaTallennettu(0, liite);
    QCOMPARE(cache.vanhin(), nullptr);
    QCOMPARE(cache.uusin(), nullptr);

    liite->vapauta();
    delete liite;

    cache.tyhjenna();
    CacheLiite* nolla = cache.liite(0);
    QCOMPARE(nolla->tila(), CacheLiite::ALUSTAMATON);
    QVERIFY(nolla->data().isEmpty());
}

void LiiteCacheTest::liiteHakematta_samaOlioIlmanHakua()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    // Kirjausnäkymän liite luodaan hakematta; myöhempi liite() palauttaa saman olion,
    // jolloin Liitteen lukitus koskee haettavaa sisältöä.
    CacheLiite* hakematta = cache.liiteHakematta(7);
    QCOMPARE(hakematta->tila(), CacheLiite::ALUSTAMATON);
    QCOMPARE(cache.liite(7), hakematta);

    QVariant vastaus(QByteArray("%PDF-1.4 sisalto"));
    cache.liiteSaapuu(7, &vastaus);
    QCOMPARE(hakematta->tila(), CacheLiite::HAETTU);
    QCOMPARE(cache.liiteHakematta(7), hakematta);
}

void LiiteCacheTest::binaariLiite_tallentuuSellaisenaan()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    // JSONilta näyttävä tavutaulukko ei saa muuttua (ei JSON-muunnosta)
    const QByteArray raaka("{ \"a\" : 1 }");
    CacheLiite* liite = lataa(cache, 1, raaka);
    QCOMPARE(liite->data(), raaka);

    // Rakenteinen vastaus tallennetaan JSONina
    CacheLiite* rakenne = cache.liite(2);
    QVariant map(QVariantMap{{QStringLiteral("a"), 1}});
    cache.liiteSaapuu(2, &map);
    QCOMPARE(QJsonDocument::fromJson(rakenne->data()).toVariant().toMap().value(QStringLiteral("a")).toInt(), 1);
}

void LiiteCacheTest::tositeValimuisti_otetaanKerran()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    cache.tositeSaapuu(10, QVariantMap{{QStringLiteral("id"), 10}, {QStringLiteral("otsikko"), QStringLiteral("A")}});
    QVariantMap tosite;
    QVERIFY(!cache.otaTosite(11, &tosite));
    QVERIFY(cache.otaTosite(10, &tosite));
    QCOMPARE(tosite.value(QStringLiteral("otsikko")).toString(), QStringLiteral("A"));
    QVERIFY(!cache.otaTosite(10, &tosite));

    cache.tositeSaapuu(12, QVariantMap{{QStringLiteral("id"), 12}});
    cache.tyhjennaTositteet();
    QCOMPARE(cache.tositteitaValimuistissa(), 0);

    cache.tositeSaapuu(13, QVariantMap{{QStringLiteral("id"), 13}});
    cache.tyhjenna();
    QCOMPARE(cache.tositteitaValimuistissa(), 0);
}

void LiiteCacheTest::tositeValimuisti_tallennusUnohtaaSamanEranTositteet()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);

    auto vienti = [](int id, int era) {
        return QVariantMap{{QStringLiteral("id"), id},
                           {QStringLiteral("era"), QVariantMap{{QStringLiteral("id"), era}}}};
    };
    // Tosite 20 on lasku (erä = sen oma vienti 200), tosite 21 sen maksu, tosite 22 ei liity
    cache.tositeSaapuu(20, QVariantMap{{QStringLiteral("id"), 20},
                                       {QStringLiteral("viennit"), QVariantList{vienti(200, 200)}}});
    cache.tositeSaapuu(21, QVariantMap{{QStringLiteral("id"), 21},
                                       {QStringLiteral("viennit"), QVariantList{vienti(210, 200)}}});
    cache.tositeSaapuu(22, QVariantMap{{QStringLiteral("id"), 22},
                                       {QStringLiteral("viennit"), QVariantList{vienti(220, 0)}}});
    cache.tositeSaapuu(23, QVariantMap{{QStringLiteral("id"), 23}});

    // Tallennetaan toinen maksu samaan erään sekä itse tosite 23
    cache.tositeTallennettu(QVariantMap{{QStringLiteral("id"), 23},
                                        {QStringLiteral("viennit"), QVariantList{vienti(230, 200)}}});

    QVariantMap tosite;
    QVERIFY(!cache.otaTosite(20, &tosite));
    QVERIFY(!cache.otaTosite(21, &tosite));
    QVERIFY(!cache.otaTosite(23, &tosite));
    QVERIFY(cache.otaTosite(22, &tosite));

    // Uusi tosite (ei vielä tunnistetta) unohtaa kaiken
    cache.tositeSaapuu(24, QVariantMap{{QStringLiteral("id"), 24}});
    cache.tositeTallennettu(QVariantMap{});
    QCOMPARE(cache.tositteitaValimuistissa(), 0);
}

void LiiteCacheTest::tositeValimuisti_kokoRajoitettu()
{
    TyhjaKitsas kitsas;
    TestattavaLiiteCache cache(nullptr, &kitsas);
    for (int i = 1; i <= 50; ++i)
        cache.tositeSaapuu(i, QVariantMap{{QStringLiteral("id"), i}});
    QVERIFY(cache.tositteitaValimuistissa() <= 6);
    QVariantMap tosite;
    QVERIFY(cache.otaTosite(50, &tosite));
}

void LiiteCacheTest::thumb_lasketaanKerranEikaPdfsta()
{
    CacheLiite pdf;
    pdf.setData(QByteArray("%PDF-1.4 ei kuva"));
    QVERIFY(pdf.thumb().isNull());

    QImage kuva(200, 100, QImage::Format_RGB32);
    kuva.fill(Qt::red);
    QByteArray png;
    QBuffer puskuri(&png);
    puskuri.open(QIODevice::WriteOnly);
    kuva.save(&puskuri, "PNG");

    CacheLiite liite;
    liite.setData(png);
    const QPixmap eka = liite.thumb();
    QVERIFY(!eka.isNull());
    QVERIFY(eka.width() <= 64 && eka.height() <= 64);
    QCOMPARE(liite.thumb().cacheKey(), eka.cacheKey());

    liite.tyhjenna();
    QVERIFY(liite.thumb().isNull());
}

QTEST_MAIN(LiiteCacheTest)
#include "tst_liitecachetest.moc"
