# Kiswas PG

This is an **unofficial modified version** of the open-source Kitsas
bookkeeping program ([artoh/kitupiikki](https://github.com/artoh/kitupiikki)).
Kitsas Oy does not support or take responsibility for this software.

Tämä on **epävirallinen muokattu versio** avoimen lähdekoodin Kitsas-ohjelmasta
([artoh/kitupiikki](https://github.com/artoh/kitupiikki)).
Kitsas Oy ei tue eikä vastaa tästä ohjelmistosta.

Ykkösversio julkaistiin nimellä [Kitupiikki](https://kitupiikki.info)

![Kiswas PG](pic/kiswas_wide.jpeg)

[![versio](https://img.shields.io/github/release/danale72/kitupiikki.svg?label=Julkaistu%20versio)](https://github.com/danale72/kitupiikki/releases)
[![versio](https://img.shields.io/github/release/danale72/kitupiikki/all.svg?label=Esiversio)](https://github.com/danale72/kitupiikki/releases)

**Finnish bookkeeping software for small organisations**

This fork adds **PostgreSQL** support (local / self-hosted server) as an
alternative to SQLite. The paid Kitsas Oy cloud service is a separate
upstream product and is unrelated to this fork.

Comments, variable names, documentations and the software itself are, of course, in Finnish only!

**Suomalainen avoimen lähdekoodin kirjanpito-ohjelma**

Alkuperäisen ohjelman kotisivu [kitsas.fi](https://kitsas.fi)
Alkuperäiset käyttöohjeet [kitsas.fi/docs](https://kitsas.fi/docs)


## Tavoitteet

- helppokäyttöisyys
- tositteiden sähköinen käsittely pdf-muodossa
- sähköisen arkiston muodostaminen
- sisäänrakennettu laskutus
- muodostaa tuloslaskelman, taseen, tase-erittelyn
- PostgreSQL-tuki paikalliselle / itse ylläpidetylle palvelimelle (SQLite-vaihtoehdon rinnalle)

Kirjanpito on mahdollista tallentaa omalle tietokoneelle SQLite-muodossa (`.kitsas`) tai paikalliselle / itse ylläpidetylle PostgreSQL-palvelimelle. Alkuperäisen Kitsas-ohjelman maksullinen pilvipalvelu on erillinen tuote; tämä fork ei ole sen osa.

## Vaatimukset
Kiswas PG käyttää [Qt-kirjastoa](https://qt.io) versio vähintään 6.4 (Kaikki ominaisuudet 6.8). Käytössä on mm. QtWidgets, QtPdf ja QtWebEngine -moduulit.

Zip-tiedostojen käsittelyyn käytetään [libzip](https://libzip.org)-kirjastoa.

Lataa ja asenna Qt-kirjastot osoitteesta https://qt.io/download.

Linuxissa poppler on helppo asentaa järjestelmään:

    sudo apt-get install libpoppler-qt5-1 libpoppler-qt5-dev

ja libzip

    sudo apt-get install libzip-dev

## Kääntäminen

Kiswas PG käyttää QMakea. Kääntäminen on helpointa tehdä [QtCreatorin](http://doc.qt.io/qtcreator/) ympäristössä. Komentorivillä koko työtila (sovellus + yksikkötestit) kääntyy komennoilla

    qmake kitsasproject.pro && make qmake_all
    make

Pelkkä sovellus:

    qmake kitsas/kitsas.pro -spec linux-g++ "CONFIG+=release"
    make


## Ylläpitäjä

Alkuperäinen ohjelma: Arto Hyvättinen <arto@kitsas.fi>

Tämän forkin ylläpitäjä: Alexei Danilov (danale72) <comradexivanov@gmail.com>

Tukea tälle muokatulle versiolle antaa forkin ylläpitäjä, ei Kitsas Oy.

## Lisenssi

GNU General Public License 3 — katso [LICENSE](LICENSE) ja [NOTICE](NOTICE).

Alkuperäiset lisäehdot (GPL §7) pätevät edelleen:

- ohjelmisto on merkittävä selkeästi muutetuksi
- on esitettävä selkeästi, ettei Kitsas Oy tarjoa mitään tukea muokatulle ohjelmistolle
- Kitsas Oy:n nimeä ei käytetä muokatun ohjelmiston tunnuksena
