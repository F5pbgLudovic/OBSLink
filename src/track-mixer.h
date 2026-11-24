// OBSLink - « Son désactivé » par source audio (voie du mélangeur d'OBS).
//
// Principe : pour une cible dont certaines sources sont désactivées, OBSLink réserve une piste OBS libre
// (en partant de la piste 6) et y envoie TOUTES les sources audio actives SAUF celles désactivées.
// Le mélangeur d'OBS fait le travail (filtres, volume, muet respectés). À l'arrêt de la cible, l'affectation
// d'origine de la piste est remise telle qu'elle était. Licence GPL v2 ou ultérieure.
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace obslink {

struct AudioSourceInfo {
    std::string name;
    bool mutedInObs = false;
};

// Sources audio actuellement présentes dans le mélangeur d'OBS (Audio du bureau, Micro, VLC, ...).
std::vector<AudioSourceInfo> ListAudioSources();

class SourceRoute;
using SourceRoutePtr = std::shared_ptr<SourceRoute>;

// Un routage par configuration audio (partagé entre les cibles qui partagent l'encodeur).
// Renvoie nullptr si aucune piste libre n'est disponible.
SourceRoutePtr AcquireRoute(const std::string& configId, const std::vector<std::string>& mutedSources);

class SourceRoute {
public:
    ~SourceRoute();
    SourceRoute(const SourceRoute&) = delete;
    SourceRoute& operator=(const SourceRoute&) = delete;

    int Track() const; // 0..5 : piste OBS utilisée par l'encodeur de la cible
    void SetMuted(const std::vector<std::string>& mutedSources); // modifiable en direct

private:
    friend SourceRoutePtr AcquireRoute(const std::string&, const std::vector<std::string>&);
    SourceRoute();
    struct Impl;
    Impl* impl_;
};

} // namespace obslink

namespace obslink {
// À appeler quand la collection de scènes est chargée : si OBS s'est arrêté (ou a planté) pendant qu'une piste
// privée était réservée, rétablit l'affectation d'origine des sources d'après le journal laissé sur le disque.
void RepairInterruptedRoutes();
}
