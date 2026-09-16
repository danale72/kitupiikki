#include <QTest>
#include <QApplication>
#include <QFileInfo>

#include <map>
#include <memory>

#include "kpdateedittesti.h"
#include "tuontitesti.h"
#include "tilitesti.h"
#include "tulomenorivitesti.h"
#include "tulomenoapuritesti.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QStringList arguments = QCoreApplication::arguments();

    std::map<QString, std::unique_ptr<QObject>> tests;

    // Tässä kaikki suoritettavat testit!
    tests.emplace("kpdateedit", std::make_unique<KpDateEditTesti>());
    tests.emplace("tuonti", std::make_unique<TuontiTesti>());
    tests.emplace("tili", std::make_unique<TiliTesti>());
    tests.emplace("tulomenorivi", std::make_unique<TuloMenoRiviTesti>());
    tests.emplace("tulomenoapuri", std::make_unique<TulomenoApuriTesti>());


    // Mahdollisuus testin valintaan
    if( arguments.size() >= 3 && arguments[1] == "-select") {

        QString testname = arguments.at(2);
        auto iter = tests.begin();
        while( iter != tests.end()) {
            if( iter->first != testname) {
                iter = tests.erase(iter);
            } else {
                ++iter;
            }
        }
        arguments.removeOne("-select");
        arguments.removeOne(testname);
    }

  int status = 0;
  for( auto& test : tests) {
      QStringList args = arguments;
      for (int i = 0; i + 1 < args.size(); ++i) {
          if (args.at(i) == "-o") {
              const QStringList osat = args.at(i + 1).split(',');
              const QFileInfo info(osat.value(0));
              QString uniq = info.dir().filePath(test.first + "-" + info.fileName());
              if (osat.size() > 1)
                  uniq += "," + osat.mid(1).join(',');
              args[i + 1] = uniq;
              break;
          }
      }
      status |= QTest::qExec( test.second.get(), args );
  }

  return status;
}
