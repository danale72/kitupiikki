# Kitsas (c) Arto Hyvättinen ja Kitsas Oy
# GPL License
#
# Tässä tiedostossa on tarvittavien kirjastojen
# määrittelyt. Muokkaa tarvittaessa tiedoston paikallista
# kopiota

# CONFIG(release, debug|release):DEFINES += QT_NO_DEBUG_OUTPUT



linux {
    DEFINES += USE_ZIPLIB
    LIBS += -lzip
}

windows {
 #   DEFINES += USE_ZIPLIB
 #   LIBS += -lzip
    LIBS += -lbcrypt
    # OpenJPEG ei ole lähdekoodissa käytössä. Linkitetään vain, jos kirjasto
    # löytyy ylläpitäjän koneelta, jotta julkaisukäännös ei kaadu ilman sitä.
    OPENJPEG_LIB = $$PWD/../../../../openjpeg-v2.5.0-windows-x64/openjpeg-v2.5.0-windows-x64/lib/openjp2.lib
    exists($$OPENJPEG_LIB) {
        LIBS += -L$$dirname(OPENJPEG_LIB) -lopenjp2
        INCLUDEPATH += $$PWD/../../../../openjpeg-v2.5.0-windows-x64/openjpeg-v2.5.0-windows-x64/include
        DEPENDPATH += $$PWD/../../../../openjpeg-v2.5.0-windows-x64/openjpeg-v2.5.0-windows-x64/include
    }
}

macx {
}


# Otetaan mukaan tiedostot, joissa määritellään
# Kitsaan käyttämät Qt-määrittelyt sekä
# lähdekoodit.

include(kitsas.pri)
include(sources.pri) 
include(pdftuonti.pri)



