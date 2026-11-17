#ifndef LIITECACHE_H
#define LIITECACHE_H

#include <QObject>
#include <QHash>
#include <QVariantMap>

class KitsasInterface;
class CacheLiite;
class QTimer;

class LiiteCache : public QObject
{
    Q_OBJECT
public:
    explicit LiiteCache(QObject *parent, KitsasInterface* kitsas);
    ~LiiteCache();

    CacheLiite* liite(int liiteId);
    /**
     * @brief Välimuistin liite ilman hakua
     *
     * Sisältö haetaan vasta, kun samalle tunnisteelle kutsutaan liite().
     * Palauttaa saman olion kuin myöhempi liite()-kutsu.
     */
    CacheLiite* liiteHakematta(int liiteId);
    void ennakkoHaku(int liiteId);

    /**
     * @brief Hakee selattavat naapuritositteet valmiiksi
     *
     * Korvaa aiemmin pyydetyt, vielä hakemattomat tositteet. Paikallisessa
     * kirjanpidossa haku viivästetään, ettei se hidasta näytettävän tositteen
     * avaamista. Tositteesta haetaan ennakkoon vain ensimmäisen liitteen sisältö.
     */
    void tositteidenEnnakkoHaku(const QList<int>& tositeIdt);
    /**
     * @brief Ottaa ennakkoon haetun tositteen välimuistista
     * @return tosi, jos tosite oli välimuistissa (se poistetaan välimuistista)
     */
    bool otaTosite(int tositeId, QVariantMap* tosite);
    /**
     * @brief Unohtaa ennakkoon haetut tositteet, joihin tallennettu tosite voi vaikuttaa
     *
     * Tallennettu tosite voi muuttaa samojen erien saldoja, joten niitä eriä
     * käyttävät tositteet haetaan uudelleen.
     */
    void tositeTallennettu(const QVariantMap& tosite);
    void tyhjennaTositteet();
    int tositteitaValimuistissa() const { return tositteet_.count(); }

    void tyhjenna();
    void lisaaTallennettu(int liiteId, CacheLiite* liite);
    void poistaPoistettu(int liiteId);

signals:
    void liiteHaettu(int liiteId);
    void hakuVirhe(int virhe, int liiteId);

protected:
    CacheLiite* haku(int liiteId, bool ennakkohaku);

    bool kaynnistaHaku(int liiteId);
    void liiteSaapuu(int liiteId, QVariant* data);
    void kaynnistaTositeHaut();
    void tositeSaapuu(int tositeId, const QVariantMap& tosite);

    void karkeen(CacheLiite* liite);
    void irrotaListasta(CacheLiite* liite);

    QHash<int,CacheLiite*> liitteet_;
    KitsasInterface* kitsas_;

    CacheLiite* uusin_ = nullptr;
    CacheLiite* vanhin_ = nullptr;

    qsizetype koko_ = 0L;

    qsizetype rajaKoko_ = 1024L * 1024L * 80L; // 80 Mt

    QHash<int,QVariantMap> tositteet_;
    QList<int> odottavatTositteet_;
    QTimer* tositeAjastin_ = nullptr;
    // Kasvaa, kun tositteet unohdetaan: ennen sitä aloitetun haun vastaus hylätään
    int tositeSukupolvi_ = 0;
    static constexpr int TOSITTEITA_ENINTAAN = 6;

};

#endif // LIITECACHE_H
