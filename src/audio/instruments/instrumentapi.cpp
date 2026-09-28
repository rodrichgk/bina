#include "instrumentapi.h"

#include <algorithm>

InstrumentParam InstrumentParam::continuous(const QString& id, const QString& label, double min, double max,
                                            double defaultValue, const QString& unit, double displayScale,
                                            int decimals, bool logarithmic)
{
    InstrumentParam p;
    p.id = id;
    p.label = label;
    p.kind = Kind::Continuous;
    p.min = min;
    p.max = max;
    p.defaultValue = defaultValue;
    p.unit = unit;
    p.displayScale = displayScale;
    p.decimals = decimals;
    p.logarithmic = logarithmic;
    return p;
}

InstrumentParam InstrumentParam::choice(const QString& id, const QString& label, const QStringList& choices,
                                        int defaultIndex)
{
    InstrumentParam p;
    p.id = id;
    p.label = label;
    p.kind = Kind::Choice;
    p.min = 0;
    p.max = qMax(0, int(choices.size()) - 1);
    p.defaultValue = defaultIndex;
    p.choices = choices;
    return p;
}

InstrumentParam InstrumentParam::toggle(const QString& id, const QString& label, bool defaultOn)
{
    InstrumentParam p;
    p.id = id;
    p.label = label;
    p.kind = Kind::Toggle;
    p.min = 0;
    p.max = 1;
    p.defaultValue = defaultOn ? 1 : 0;
    return p;
}

const InstrumentParam* InstrumentDefinition::findParam(const QString& paramId) const
{
    for (const InstrumentParam& p : params) {
        if (p.id == paramId) {
            return &p;
        }
    }
    return nullptr;
}

double InstrumentContext::param(const QString& id) const
{
    const InstrumentParam* p = definition.findParam(id);
    if (!p) {
        return 0.0;
    }
    const auto it = settings.params.constFind(id);
    const double value = it != settings.params.constEnd() ? it.value() : p->defaultValue;
    return qBound(p->min, value, p->max);
}

namespace {

// Function-local so registration from other files' static initializers is safe
QVector<InstrumentDefinition>& registry()
{
    static QVector<InstrumentDefinition> definitions;
    return definitions;
}

} // namespace

namespace InstrumentRegistry {

bool add(const InstrumentDefinition& definition)
{
    registry().append(definition);
    return true;
}

QVector<const InstrumentDefinition*> all()
{
    QVector<const InstrumentDefinition*> sorted;
    for (const InstrumentDefinition& d : registry()) {
        sorted.append(&d);
    }
    std::sort(sorted.begin(), sorted.end(), [](const InstrumentDefinition* a, const InstrumentDefinition* b) {
        return a->sortOrder != b->sortOrder ? a->sortOrder < b->sortOrder : a->name < b->name;
    });
    return sorted;
}

const InstrumentDefinition* find(const QString& id)
{
    for (const InstrumentDefinition& d : registry()) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

std::unique_ptr<Instrument> create(const InstrumentSettings& settings, int sampleRate)
{
    const InstrumentDefinition* definition = find(settings.type);
    if (!definition) {
        const auto list = all();
        if (list.isEmpty()) {
            return nullptr;
        }
        definition = list.first();
    }
    const InstrumentContext context{settings, *definition, sampleRate};
    return definition->create(context);
}

} // namespace InstrumentRegistry
