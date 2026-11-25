#pragma once
// OBSLink 1.01 : docks de chat kick et YouTube (navigateur integre d'OBS)
namespace obslink {
// A appeler depuis obs_module_load (thread UI) : cree les deux docks.
void CreateChatDocks();
// A appeler sur OBS_FRONTEND_EVENT_FINISHED_LOADING : charge les pages memorisees.
void OnChatDocksFinishedLoading();
// A la fermeture d'OBS : memorise la taille des docks.
void OnChatDocksExit();
}
