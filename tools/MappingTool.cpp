// mapping-tool — the home for everything coupled to the trigger note mapping.
//
// It links the plugin's TriggerVocabulary (the single source of truth), so it
// never re-derives the mapping. Today it can dump and verify the frozen
// snapshot; the migration converter (old→new) and the RGB-clip importer will
// land here as further subcommands, all sharing that one vocabulary.
//
//   mapping-tool dump              print the current snapshot (note <tab> name)
//   mapping-tool version           print the current mapping version
//   mapping-tool check <dir>       compare current map to <dir>/v<version>.tsv
//
// Freeze a new version: bump vocab::kMappingVersion, then
//   mapping-tool dump > mappings/v<N>.tsv   and commit it.

#include <juce_core/juce_core.h>

#include <iostream>

#include "TriggerVocabulary.h"

using namespace hitnotedmx;

namespace
{
// The frozen-snapshot text: 128 rows of "note<TAB>chainName\n".
juce::String currentSnapshot()
{
    juce::String s;
    for (int note = 0; note < 128; ++note)
        s << note << '\t' << vocab::chainName (note) << '\n';
    return s;
}

int doCheck (const juce::File& dir)
{
    const auto snap = dir.getChildFile ("v" + juce::String (vocab::kMappingVersion) + ".tsv");
    if (! snap.existsAsFile())
    {
        std::cerr << "missing snapshot: " << snap.getFullPathName() << '\n'
                  << "freeze it with:  mapping-tool dump > " << snap.getFullPathName() << '\n';
        return 1;
    }

    const auto expected = snap.loadFileAsString();
    const auto actual   = currentSnapshot();
    if (actual == expected)
    {
        std::cout << "mapping matches v" << vocab::kMappingVersion << " ("
                  << snap.getFileName() << ")\n";
        return 0;
    }

    // Report the first differing row to make the drift obvious.
    juce::StringArray a, e;
    a.addLines (actual);
    e.addLines (expected);
    for (int i = 0; i < juce::jmax (a.size(), e.size()); ++i)
    {
        const auto la = i < a.size() ? a[i] : juce::String ("<end>");
        const auto le = i < e.size() ? e[i] : juce::String ("<end>");
        if (la != le)
        {
            std::cerr << "MISMATCH vs " << snap.getFileName() << " at row " << i << ":\n"
                      << "  frozen:  " << le << '\n'
                      << "  current: " << la << '\n'
                      << "If the change is intentional, bump kMappingVersion and re-freeze;\n"
                      << "otherwise this is an accidental mapping change.\n";
            break;
        }
    }
    return 1;
}
}  // namespace

int main (int argc, char** argv)
{
    const juce::String cmd = argc > 1 ? juce::String (argv[1]) : juce::String ("dump");

    if (cmd == "dump")
    {
        std::cout << currentSnapshot();
        return 0;
    }
    if (cmd == "version")
    {
        std::cout << vocab::kMappingVersion << '\n';
        return 0;
    }
    if (cmd == "check")
    {
        const juce::File dir = argc > 2 ? juce::File (juce::String (argv[2]))
                                        : juce::File::getCurrentWorkingDirectory();
        return doCheck (dir);
    }

    std::cerr << "usage: mapping-tool [dump | version | check <mappings-dir>]\n";
    return 2;
}
