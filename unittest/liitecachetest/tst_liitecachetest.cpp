/*
   Copyright (C) 2026 Alexei Danilov

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include <QtTest>

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

QTEST_MAIN(LiiteCacheTest)
#include "tst_liitecachetest.moc"
