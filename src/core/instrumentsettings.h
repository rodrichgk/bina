#ifndef INSTRUMENTSETTINGS_H
#define INSTRUMENTSETTINGS_H

#include "audiobuffer.h"

#include <QMap>
#include <QString>
#include <memory>

// What a track stores about its instrument. Pure data: which instrument type and
// the values of its parameters. The parameter list itself (names, ranges,
// defaults) is declared by each instrument in src/audio/instruments/.
struct InstrumentSettings {
    QString type = "synth";          // InstrumentDefinition::id
    QMap<QString, double> params;    // parameter id -> value; missing means "use the default"

    // For instruments that play a loaded audio file (InstrumentDefinition::usesSample)
    QString samplePath;
    std::shared_ptr<const AudioBuffer> sample;
};

#endif // INSTRUMENTSETTINGS_H
