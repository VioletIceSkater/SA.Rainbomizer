#include "cutscenes.hh"
#include <cstdlib>
#include "logger.hh"
#include "base.hh"
#include <vector>
#include "functions.hh"
#include <algorithm>
#include "config.hh"
#include "scrpt.hh"
#include "injector/calling.hpp"
#include "loader.hh"

CutsceneRandomizer *CutsceneRandomizer::mInstance = nullptr;

static std::string model = "";

/*******************************************************/
char *
RandomizeCutsceneObject (char *dst, char *src)
{
    char *ret = CallAndReturn<char *, 0x82244B> (dst, src);
    return CutsceneRandomizer::GetInstance ()->GetRandomModel (ret);
}

/*******************************************************/
int
LoadModelForCutscene (std::string name)
{
    // Set ms_cutsceneProcessing to 0 before loading.
    // The game doesn't load the models without this for some reason
    injector::WriteMemory<uint8_t> (0xB5F852, 0);
    int ret = 1;

    short modelIndex = 0;
    CModelInfo::GetModelInfo (name.c_str (), &modelIndex);

    Logger::GetLogger ()->LogMessage ("Loading Cutscene Model: "
                                      + name + " " + std::to_string(modelIndex));
    
    if (modelIndex && StreamingManager::AttemptToLoadVehicle (modelIndex) == ERR_FAILED)
        {
            Logger::GetLogger ()->LogMessage ("Failed to load Cutscene Model: "
                                              + name);
            ret = 0;
        }

    injector::WriteMemory<uint8_t> (0xB5F852, 1);
    return ret;
}

/*******************************************************/
char *
CutsceneRandomizer::GetRandomModel (std::string model)
{
    std::transform (std::begin (model), std::end (model), model.begin (),
                    [] (unsigned char c) { return std::tolower (c); });
    mLastModel = model;

    for (auto i : mModels)
        {
            if (std::find (std::begin (i), std::end (i), model) != std::end (i))
                {
                    auto replaced = i[random (i.size () - 1)];
                    mLastModel    = replaced;
                    break;
                }
        }
    if (!LoadModelForCutscene (mLastModel))
        mLastModel = model;

    return (char *) mLastModel.c_str ();
}

/*******************************************************/
void
RandomizeCutsceneOffset (char *Str, char *format, float *x, float *y, float *z)
{

    auto cutsceneRandomizer = CutsceneRandomizer::GetInstance ();

    sscanf (Str, format, x, y, z);

    cutsceneRandomizer->originalLevel = injector::ReadMemory<int> (0xB72914);
    Scrpt::CallOpcode (0x4BB, "select_interior", 0);

    *x = randomFloat (-3000, 3000);
    *y = randomFloat (-3000, 3000);

    Scrpt::CallOpcode (0x4E4, "refresh_game_renderer", *x, *y);
    Scrpt::CallOpcode (0x3CB, "set_render_origin", *x, *y, 20);
    Scrpt::CallOpcode (0x15f, "set_pos", *x, *y, 20, 0, 0, 0);

    *z = CWorld::FindGroundZedForCoord (*x, *y);
}

/*******************************************************/
bool
RestoreCutsceneInterior ()
{
    bool ret = HookManager::CallOriginalAndReturn<injector::cstd<bool ()>,
                                                  0x480761> (false);
    if (ret)
        {
            auto cutsceneRandomizer = CutsceneRandomizer::GetInstance ();
            Scrpt::CallOpcode (0x4BB, "select_interior",
                               cutsceneRandomizer->originalLevel);
        }
    return ret;
}

/*******************************************************/
void
CutsceneRandomizer::Initialise ()
{
    auto config = ConfigManager::GetInstance ()->GetConfigs ().cutscenes;
    if (!config.enabled)
        return;

    FILE *modelFile = OpenRainbomizerFile (config.cutsceneFile, "r");
    if (modelFile && config.randomizeModels)
        {
            char line[512] = {0};
            mModels.push_back (std::vector<std::string> ());
            while (fgets (line, 512, modelFile))
                {
                    if (strlen (line) < 2)
                        {
                            mModels.push_back (std::vector<std::string> ());
                            continue;
                        }
                    line[strcspn (line, "\n")] = 0;
                    mModels.back ().push_back (line);
                }
            RegisterHooks (
                {{HOOK_CALL, 0x5B0B30, (void *) &RandomizeCutsceneObject}});
        }
    else if (!modelFile)
        {
            // Log a message if file wasn't found
            Logger::GetLogger ()->LogMessage (
                "Failed to read file: rainbomizer/" + config.cutsceneFile);
            Logger::GetLogger ()->LogMessage (
                "Cutscene models will not be randomized");
        }

    if (config.randomizeLocations)
        {
            RegisterHooks (
                {{HOOK_CALL, 0x5B0A1F, (void *) &RandomizeCutsceneOffset},
                 {HOOK_CALL, 0x480761, (void *) &RestoreCutsceneInterior}});
            injector::MakeNOP (0x5B09D2, 5);
        }

    Logger::GetLogger ()->LogMessage ("Intialised CutsceneRandomizer");
}

/*******************************************************/
void
CutsceneRandomizer::DestroyInstance ()
{
    if (CutsceneRandomizer::mInstance)
        delete CutsceneRandomizer::mInstance;
}

/*******************************************************/
CutsceneRandomizer *
CutsceneRandomizer::GetInstance ()
{
    if (!CutsceneRandomizer::mInstance)
        {
            CutsceneRandomizer::mInstance = new CutsceneRandomizer ();
            atexit (&CutsceneRandomizer::DestroyInstance);
        }
    return CutsceneRandomizer::mInstance;
}
