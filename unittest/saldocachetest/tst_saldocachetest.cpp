/*
   Copyright (C) 2026 Kitsas contributors

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/
#include <QtTest>

#include "sql/saldocache.h"

class SaldoCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void vainSaldotValimuistiin();
    void avainErotteleeParametrit();
    void tallennaJaHae();
    void tyhjennaPoistaa();
    void kokoRajoitettu();
};

void SaldoCacheTest::vainSaldotValimuistiin()
{
    QVERIFY( SaldoCache::onkoValimuistissa("/saldot"));
    QVERIFY( !SaldoCache::onkoValimuistissa("/saldot/1"));
    QVERIFY( !SaldoCache::onkoValimuistissa("/tositteet"));
    QVERIFY( !SaldoCache::onkoValimuistissa("/viennit"));
}

void SaldoCacheTest::avainErotteleeParametrit()
{
    QCOMPARE( SaldoCache::avain("/saldot", "pvm=2026-12-31"), SaldoCache::avain("/saldot", "pvm=2026-12-31"));
    QVERIFY( SaldoCache::avain("/saldot", "pvm=2026-12-31") != SaldoCache::avain("/saldot", "pvm=2025-12-31"));
    QVERIFY( SaldoCache::avain("/saldot", "pvm=2026-12-31") != SaldoCache::avain("/saldot", "pvm=2026-12-31&tili=1910"));
}

void SaldoCacheTest::tallennaJaHae()
{
    SaldoCache cache;
    QVariant tulos;
    const QString avain = SaldoCache::avain("/saldot", "pvm=2026-12-31");
    QVERIFY( !cache.hae(avain, &tulos));

    QVariantMap saldot{{"1910", 123.45}};
    cache.tallenna(avain, saldot);
    QVERIFY( cache.hae(avain, &tulos));
    QCOMPARE( tulos.toMap(), saldot);

    QVERIFY( !cache.hae(SaldoCache::avain("/saldot", "pvm=2025-12-31"), &tulos));
}

void SaldoCacheTest::tyhjennaPoistaa()
{
    SaldoCache cache;
    const QString avain = SaldoCache::avain("/saldot", "pvm=2026-12-31");
    cache.tallenna(avain, QVariantMap{{"1910", 1.0}});
    cache.tyhjenna();

    QVariant tulos;
    QVERIFY( !cache.hae(avain, &tulos));
    QCOMPARE( cache.koko(), 0);
}

void SaldoCacheTest::kokoRajoitettu()
{
    SaldoCache cache;
    for(int i=0; i < 1000; i++)
        cache.tallenna(SaldoCache::avain("/saldot", QString("tili=%1").arg(i)), i);
    QVERIFY( cache.koko() <= 64);
    QVariant tulos;
    QVERIFY( cache.hae(SaldoCache::avain("/saldot", "tili=999"), &tulos));
    QCOMPARE( tulos.toInt(), 999);
}

QTEST_APPLESS_MAIN(SaldoCacheTest)

#include "tst_saldocachetest.moc"
