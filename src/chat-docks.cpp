#include "pch.h"
#include "chat-docks.h"
#include "browser-panel.hpp"
#include "json.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QMenu>
#include <QDialog>
#include <QSlider>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QElapsedTimer>
#include <fstream>
#include <Windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace obslink {

static QCef* g_cef = nullptr;
static bool g_loaded = false;

static std::string ConfigFile()
{
    char dir[512];
    if (os_get_config_path(dir, sizeof(dir), "obs-studio/plugin_config/OBSLink") <= 0)
        return std::string();
    os_mkdirs(dir);
    return std::string(dir) + "/chat.json";
}

static nlohmann::json ReadConfig()
{
    try {
        std::string f = ConfigFile();
        if (f.empty()) return nlohmann::json::object();
        std::ifstream in(f);
        if (!in) return nlohmann::json::object();
        auto j = nlohmann::json::parse(in, nullptr, false);
        return j.is_object() ? j : nlohmann::json::object();
    } catch (...) { return nlohmann::json::object(); }
}

static void WriteConfig(const nlohmann::json& j)
{
    try {
        std::string f = ConfigFile();
        if (f.empty()) return;
        std::ofstream out(f, std::ios::trunc);
        out << j.dump(2);
    } catch (...) {}
}

struct SoundCfg {
    bool enabled = false;
    std::string file = "C:/Windows/Media/Windows Notify Messaging.wav";
    int volume = 60; // 0..100
    bool dark = true; // mode sombre des pages de chat
};

// Script injecte dans la page : detecte les nouveaux messages et change le titre
// de la page (document.title = OBSLINK-MSG-n) ; le plugin reagit au changement de titre.
static const char* kWatchScript = R"JS(
(function(){
 if (window.__obslinkWatch) return; window.__obslinkWatch = 1;
 var n = 0, t0 = Date.now();
 var sel = 'yt-live-chat-text-message-renderer,yt-live-chat-paid-message-renderer,yt-live-chat-membership-item-renderer,'
         + '[data-chat-entry],[data-a-target="chat-line-message"],.chat-line__message,.chat-entry,'
         + '[class*="chat-entry"],[class*="chat-message"],[class*="chatroom-message"],[class*="message-item"],[data-testid*="message"],[id^="message"],div[data-index][class*="inset-x-0"]';
 // un meme message (texte deja vu) ne sonne qu'une fois : les listes virtuelles redessinent des lignes deja affichees
 var recent = {}, order = [];
 // memoire conservee entre deux chargements de la page (la page kick se recharge et reaffiche son historique)
 try { order = JSON.parse(localStorage.getItem('obslink_recent') || '[]').slice(-400); order.forEach(function(x){ recent[x] = 1; }); } catch (e) { order = []; }
 function check(el){
   var tx = (el.textContent || '').replace(/\s+/g, ' ').trim();
   if (tx.length < 2 || recent[tx]) return;
   // kick : une vraie ligne contient Â« pseudo: texte Â» ; on ignore les bandeaux de l'interface (ex. Â« Nouveaux messages Â»)
   if (isKick && (tx.indexOf(':') < 1 || /^(Nouveaux? messages?|New messages?)/i.test(tx))) return;
   // kick affiche l'heure (HH:MM) devant chaque ligne : un message de plus de ~75 s est de l'historique
   var hm = tx.match(/^(\d{2}):(\d{2})/);
   if (isKick && hm) {
     var d = new Date(), age = (d.getHours() * 3600 + d.getMinutes() * 60 + d.getSeconds()) - (parseInt(hm[1], 10) * 3600 + parseInt(hm[2], 10) * 60);
     if (age < 0) age += 86400;
     if (age > 75) { recent[tx] = 1; return; }
   }
   recent[tx] = 1; order.push(tx);
   if (order.length > 400) delete recent[order.shift()];
   try { localStorage.setItem('obslink_recent', JSON.stringify(order)); } catch (e) {}
   n++; document.title = 'OBSLINK-MSG-' + n + '|' + tx.slice(0, 100);
 }
 function isMsg(e){ return e.nodeType === 1 && ((e.matches && e.matches(sel)) || (e.querySelector && e.querySelector(sel))); }
 var isKick = /kick\.com/.test(location.host), seen = {}, nseen = 0, q = [], DEBUG = false, quiet = isKick ? 12000 : 4000;
 if (isKick) sel = 'div[data-index][class*="inset-x-0"]'; // kick : uniquement les lignes de message de la liste
 function sig(e){
   var a = []; for (var i = 0; i < e.attributes.length && i < 6; i++) a.push(e.attributes[i].name);
   return e.tagName.toLowerCase() + (e.id ? '#' + e.id.slice(0, 20) : '') + '.' + String(e.className && e.className.baseVal === undefined ? e.className : '').slice(0, 60).replace(/\s+/g, '.') + '[' + a.join(',') + ']';
 }
 // diagnostic kick : signatures des elements ajoutes, relues dans le journal d'OBS
 if (isKick && DEBUG) setInterval(function(){ if (q.length) document.title = 'OBSLINK-DBG-' + q.shift(); }, 400);
 new MutationObserver(function(ms){
   if (Date.now() - t0 < quiet) return;
   if (isKick && DEBUG) for (var i = 0; i < ms.length; i++) for (var k = 0; k < ms[i].addedNodes.length; k++) {
     var e = ms[i].addedNodes[k];
     if (e.nodeType === 1 && nseen < 30) { var s = sig(e); if (!seen[s]) { seen[s] = 1; nseen++; q.push(s); } }
   }
   for (var i = 0; i < ms.length; i++) {
     var a = ms[i].addedNodes;
     for (var k = 0; k < a.length; k++) {
       var nd = a[k];
       if (nd.nodeType !== 1) continue;
       var el = (nd.matches && nd.matches(sel)) ? nd : (nd.querySelector ? nd.querySelector(sel) : null);
       if (el) (function(el){ setTimeout(function(){ check(el); }, 250); })(el);
     }
   }
 }).observe(document.documentElement, {childList: true, subtree: true});
})();
)JS";

// Page /live d'une chaine (@nom) : on repere le direct en cours (lien canonique) puis on ouvre son chat.
static const char* kYtLiveScript = R"JS(
(function(){
 if (!/youtube\.com\/(@|channel\/|c\/)[^\/]+\/live/.test(location.href)) return;
 var tries = 0;
 var t = setInterval(function(){
   var c = document.querySelector('link[rel=canonical]');
   var m = c && c.href.match(/[?&]v=([\w-]{11})/);
   if (m) { clearInterval(t); location.href = 'https://www.youtube.com/live_chat?is_popout=1@DARK@&v=' + m[1]; return; }
   // pas (encore) de direct : on recharge la page toutes les 30 s pour detecter son debut
   if (++tries % 60 === 0) location.reload();
 }, 500);
})();
)JS";

static void ApplyReferenceSize();
static int g_aliasCounter = 0;

class ChatDock : public QWidget
{
    Q_OBJECT
public slots:
    void OnUrl(const QString& u) { blog(LOG_INFO, TAG "chat %s : page %s", Key(), tostdu8(u.left(160)).c_str()); }
    void OnTitle(const QString& t)
    {
        if (t.startsWith("OBSLINK-DBG-")) {
            blog(LOG_INFO, TAG "chat %s : element ajoute %s", Key(), tostdu8(t.mid(12).left(200)).c_str());
            return;
        }
        if (!t.startsWith("OBSLINK-MSG-")) return;
        // le navigateur renotifie parfois un titre inchange (toutes les 60 s sur kick) : ce n'est pas un nouveau message
        if (t == lastTitle_) return;
        lastTitle_ = t;
        blog(LOG_INFO, TAG "chat %s : nouveau message detecte (%s, son %s)", Key(), tostdu8(t).c_str(), cfg_.enabled ? "actif" : "coupe");
        OnNewMessage();
    }
public:
    enum Kind { Kick, YouTube, Twitch };

    ChatDock(Kind kind) : kind_(kind)
    {
        auto lay = new QVBoxLayout(this);
        lay->setContentsMargins(4, 4, 4, 4);
        auto row = new QHBoxLayout();
        edit_ = new QLineEdit();
        const char* ph = kind == Kick ? "Chat.Kick.Placeholder" : kind == YouTube ? "Chat.YouTube.Placeholder" : "Chat.Twitch.Placeholder";
        edit_->setPlaceholderText(QString::fromUtf8(obs_module_text(ph)));
        go_ = new QPushButton(QString::fromUtf8(obs_module_text("Chat.Show")));
        sound_ = new QPushButton(QString::fromUtf8("\xE2\x99\xAA")); // note de musique
        sound_->setToolTip(QString::fromUtf8(obs_module_text("Chat.SoundTip")));
        sound_->setFixedWidth(32);
        row->addWidget(edit_, 1);
        row->addWidget(go_);
        row->addWidget(sound_);
        lay->addLayout(row);
        info_ = new QLabel(QString::fromUtf8(obs_module_text("Chat.Hint")));
        info_->setWordWrap(true);
        lay->addWidget(info_);
        lay_ = lay;
        alias_ = "obslinkchat" + std::to_string(++g_aliasCounter);
        connect(go_, &QPushButton::clicked, this, [this]() { Apply(true); });
        connect(edit_, &QLineEdit::returnPressed, this, [this]() { Apply(true); });
        connect(sound_, &QPushButton::clicked, this, [this]() { ShowMenu(QCursor::pos()); });
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& p) { ShowMenu(mapToGlobal(p)); });
        timer_.start();
        setMinimumSize(260, 240);
        resize(404, 952);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    ~ChatDock() { StopSound(); }

    QSize sizeHint() const override { return pref_.isValid() ? pref_ : QSize(404, 952); }

    // Taille preferee : la sienne si memorisee, sinon celle du dock Twitch (reference).
    void SetPreferredSize(const QSize& s) { if (s.isValid()) { pref_ = s; resize(s); } }
    void SaveSizeNow()
    {
        if (!isVisible() || width() < 200 || height() < 200) return;
        auto j = ReadConfig();
        j[std::string(Key()) + "_size"] = { width(), height() };
        WriteConfig(j);
    }
    bool IsReference() const { return kind_ == Twitch; }
    static QSize LoadSize(const char* key)
    {
        auto j = ReadConfig();
        std::string k = std::string(key) + "_size";
        if (j.contains(k) && j[k].is_array() && j[k].size() == 2)
            return QSize(j[k][0].get<int>(), j[k][1].get<int>());
        return QSize();
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QWidget::resizeEvent(e);
        if (!isVisible() || width() < 200 || height() < 200) return;
        pref_ = size();
        if (!saveTimer_) {
            saveTimer_ = new QTimer(this);
            saveTimer_->setSingleShot(true);
            connect(saveTimer_, &QTimer::timeout, this, [this]() {
                auto j = ReadConfig();
                j[std::string(Key()) + "_size"] = { pref_.width(), pref_.height() };
                WriteConfig(j);
            });
        }
        saveTimer_->start(1500);
    }

public:
    QSize minimumSizeHint() const override { return QSize(260, 240); }

    const char* Key() const { return kind_ == Kick ? "kick" : kind_ == YouTube ? "youtube" : "twitch"; }

    void LoadSaved()
    {
        auto j = ReadConfig();
        if (j.contains(Key()) && j[Key()].is_string())
            edit_->setText(QString::fromUtf8(j[Key()].get<std::string>().c_str()));
        {
            QSize s = LoadSize(Key());
            if (!s.isValid()) s = LoadSize("twitch"); // les autres docks prennent la taille du dock Twitch
            SetPreferredSize(s);
        }
        std::string sk = std::string(Key()) + "_sound";
        if (j.contains(sk) && j[sk].is_object()) {
            auto& s = j[sk];
            cfg_.enabled = s.value("enabled", false);
            cfg_.file = s.value("file", cfg_.file);
            cfg_.volume = std::max(0, std::min(100, s.value("volume", 60)));
            cfg_.dark = s.value("dark", true);
        }
        if (edit_->text().trimmed().isEmpty()) {
            // rien de saisi : nom de compte memorise par OBS dans le profil (aucun jeton n'est lu)
            config_t* pc = obs_frontend_get_profile_config();
            const char* v = nullptr;
            if (pc && kind_ == Twitch) v = config_get_string(pc, "Twitch", "Name");
            if (pc && kind_ == YouTube) v = config_get_string(pc, "YouTube", "ChannelName");
            // kick : OBS ne garde pas le nom de la chaine ; on essaie le meme pseudo que Twitch (modifiable ensuite)
            if (pc && kind_ == Kick) { v = config_get_string(pc, "Twitch", "Name"); if (!v || !*v) v = config_get_string(pc, "YouTube", "ChannelName"); }
            if (v && *v) edit_->setText((kind_ == YouTube ? QString("@") : QString()) + QString::fromUtf8(v));
        }
        if (!edit_->text().trimmed().isEmpty())
            Apply(false);
    }

private:
    QString BuildUrl(const QString& in) const
    {
        QString s = in.trimmed();
        if (s.isEmpty()) return QString();
        if (kind_ == Kick) {
            auto m = QRegularExpression(R"(kick\.com/(?:popout/)?([A-Za-z0-9_\-]+))").match(s);
            QString name = m.hasMatch() ? m.captured(1) : s;
            if (!QRegularExpression(R"(^[A-Za-z0-9_\-]+$)").match(name).hasMatch()) return QString();
            name = name.toLower().replace('_', '-');
            return "https://kick.com/popout/" + name + "/chat";
        }
        if (kind_ == Twitch) {
            auto m = QRegularExpression(R"(twitch\.tv/(?:popout/)?([A-Za-z0-9_]+))").match(s);
            QString name = m.hasMatch() ? m.captured(1) : s;
            if (!QRegularExpression(R"(^[A-Za-z0-9_]+$)").match(name).hasMatch()) return QString();
            return "https://www.twitch.tv/popout/" + name.toLower() + (cfg_.dark ? "/chat?popout=&darkpopout" : "/chat?popout=");
        }
        if (QRegularExpression(R"(^@[A-Za-z0-9_.\-]+$)").match(s).hasMatch())
            return "https://www.youtube.com/" + s + "/live";
        auto m = QRegularExpression(R"((?:[?&]v=|youtu\.be/|/live/|/embed/)([A-Za-z0-9_\-]{11}))").match(s);
        QString id = m.hasMatch() ? m.captured(1) : s;
        if (!QRegularExpression(R"(^[A-Za-z0-9_\-]{11}$)").match(id).hasMatch()) return QString();
        return QString("https://www.youtube.com/live_chat?is_popout=1") + (cfg_.dark ? "&dark_theme=1" : "") + "&v=" + id;
    }

    void Apply(bool save)
    {
        QString url = BuildUrl(edit_->text());
        if (url.isEmpty()) {
            info_->setText(QString::fromUtf8(obs_module_text("Chat.BadInput")));
            info_->show();
            return;
        }
        if (!g_cef) {
            info_->setText(QString::fromUtf8(obs_module_text("Chat.NoBrowser")));
            info_->show();
            return;
        }
        if (save) {
            auto j = ReadConfig();
            j[Key()] = tostdu8(edit_->text().trimmed());
            WriteConfig(j);
        }
        info_->hide();
        if (web_) {
            web_->setURL(url.toStdString());
        } else {
            g_cef->wait_for_browser_init();
            web_ = g_cef->create_widget(this, url.toStdString(), nullptr);
            if (web_) {
                std::string ytScript = kYtLiveScript;
                ytScript.replace(ytScript.find("@DARK@"), 6, cfg_.dark ? "&dark_theme=1" : "");
                web_->setStartupScript(std::string(kWatchScript) + ytScript);
                QObject::connect(web_, SIGNAL(titleChanged(QString)), this, SLOT(OnTitle(QString)));
                QObject::connect(web_, SIGNAL(urlChanged(QString)), this, SLOT(OnUrl(QString)));
                lay_->addWidget(web_, 1);
            }
        }
    }

    void OnNewMessage()
    {
        if (!cfg_.enabled) return;
        if (timer_.elapsed() < 800) return; // anti-rafale
        timer_.restart();
        PlaySoundNow();
    }

    void LogMci(MCIERROR err, const char* what)
    {
        wchar_t buf[256] = {0};
        mciGetErrorStringW(err, buf, 255);
        blog(LOG_WARNING, TAG "chat %s : son (%s) : %s", Key(), what, tostdu8(QString::fromWCharArray(buf)).c_str());
    }

    void StopSound()
    {
        std::wstring a(alias_.begin(), alias_.end());
        std::wstring cmd = L"close " + a;
        mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
    }

    void PlaySoundNow()
    {
        StopSound();
        std::wstring a(alias_.begin(), alias_.end());
        std::wstring f = QDir::toNativeSeparators(QString::fromUtf8(cfg_.file.c_str())).toStdWString();
        // pilote "mpegvideo" : lit aussi les .wav et accepte le reglage de volume (pas le pilote waveaudio)
        std::wstring cmd = L"open \"" + f + L"\" type mpegvideo alias " + a;
        MCIERROR err = mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
        if (err != 0) {
            cmd = L"open \"" + f + L"\" alias " + a;
            err = mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
        }
        if (err != 0) { LogMci(err, "open"); return; }
        cmd = L"setaudio " + a + L" volume to " + std::to_wstring(cfg_.volume * 10);
        err = mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
        if (err != 0) LogMci(err, "volume");
        cmd = L"play " + a + L" from 0";
        err = mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
        if (err != 0) LogMci(err, "play");
    }

    void SaveSound()
    {
        auto j = ReadConfig();
        j[std::string(Key()) + "_sound"] = { {"enabled", cfg_.enabled}, {"file", cfg_.file}, {"volume", cfg_.volume}, {"dark", cfg_.dark} };
        WriteConfig(j);
    }

    void ShowMenu(const QPoint& pos)
    {
        QMenu menu(this);
        auto actOn = menu.addAction(QString::fromUtf8(obs_module_text("Chat.SoundOn")));
        actOn->setCheckable(true);
        actOn->setChecked(cfg_.enabled);
        auto actDark = menu.addAction(QString::fromUtf8(obs_module_text("Chat.Dark")));
        actDark->setCheckable(true);
        actDark->setChecked(cfg_.dark);
        auto actSize = menu.addAction(QString::fromUtf8(obs_module_text("Chat.SameSize")));
        auto actSet = menu.addAction(QString::fromUtf8(obs_module_text("Chat.SoundSettings")));
        auto actReload = menu.addAction(QString::fromUtf8(obs_module_text("Chat.Reload")));
        auto chosen = menu.exec(pos);
        if (chosen == actOn) {
            cfg_.enabled = actOn->isChecked();
            SaveSound();
        } else if (chosen == actDark) {
            cfg_.dark = actDark->isChecked();
            SaveSound();
            Apply(false); // recharge la page avec le nouveau theme
        } else if (chosen == actSize) {
            ApplyReferenceSize();
        } else if (chosen == actSet) {
            SoundDialog();
        } else if (chosen == actReload) {
            if (web_) web_->reloadPage();
        }
    }

    void SoundDialog()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QString::fromUtf8(obs_module_text("Chat.SoundSettings")));
        auto form = new QFormLayout(&dlg);
        auto chk = new QCheckBox(QString::fromUtf8(obs_module_text("Chat.SoundOn")));
        chk->setChecked(cfg_.enabled);
        form->addRow(chk);
        auto combo = new QComboBox();
        combo->setEditable(false);
        QDir media("C:/Windows/Media");
        for (const auto& fi : media.entryInfoList({"*.wav"}, QDir::Files, QDir::Name))
            combo->addItem(fi.completeBaseName(), fi.absoluteFilePath());
        QString cur = QString::fromUtf8(cfg_.file.c_str());
        int idx = combo->findData(cur, Qt::UserRole, Qt::MatchFixedString);
        if (idx < 0) {
            combo->addItem(QFileInfo(cur).fileName(), cur);
            idx = combo->count() - 1;
        }
        combo->setCurrentIndex(idx);
        auto browse = new QPushButton(QString::fromUtf8(obs_module_text("Chat.Browse")));
        auto rowSound = new QHBoxLayout();
        rowSound->addWidget(combo, 1);
        rowSound->addWidget(browse);
        form->addRow(QString::fromUtf8(obs_module_text("Chat.Sound")), rowSound);
        auto slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(cfg_.volume);
        auto volLbl = new QLabel(QString::number(cfg_.volume) + " %");
        auto rowVol = new QHBoxLayout();
        rowVol->addWidget(slider, 1);
        rowVol->addWidget(volLbl);
        form->addRow(QString::fromUtf8(obs_module_text("Chat.Volume")), rowVol);
        auto test = new QPushButton(QString::fromUtf8(obs_module_text("Chat.Test")));
        form->addRow(test);
        auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        connect(slider, &QSlider::valueChanged, &dlg, [volLbl](int v) { volLbl->setText(QString::number(v) + " %"); });
        connect(browse, &QPushButton::clicked, &dlg, [&]() {
            QString f = QFileDialog::getOpenFileName(&dlg, QString::fromUtf8(obs_module_text("Chat.Browse")), "C:/Windows/Media",
                                                    "Audio (*.wav *.mp3 *.wma *.m4a)");
            if (f.isEmpty()) return;
            combo->addItem(QFileInfo(f).fileName(), f);
            combo->setCurrentIndex(combo->count() - 1);
        });
        connect(test, &QPushButton::clicked, &dlg, [&]() {
            SoundCfg keep = cfg_;
            cfg_.file = tostdu8(combo->currentData().toString());
            cfg_.volume = slider->value();
            PlaySoundNow();
            cfg_.file = keep.file;
            cfg_.volume = keep.volume;
        });
        if (dlg.exec() == QDialog::Accepted) {
            cfg_.enabled = chk->isChecked();
            cfg_.file = tostdu8(combo->currentData().toString());
            cfg_.volume = slider->value();
            SaveSound();
        }
        StopSound();
    }

    Kind kind_;
    QLineEdit* edit_ = nullptr;
    QPushButton* go_ = nullptr;
    QPushButton* sound_ = nullptr;
    QLabel* info_ = nullptr;
    QVBoxLayout* lay_ = nullptr;
    QCefWidget* web_ = nullptr;
    SoundCfg cfg_;
    std::string alias_;
    QElapsedTimer timer_;
    QSize pref_;
    QString lastTitle_;
    QTimer* saveTimer_ = nullptr;
};

static ChatDock* g_docks[3] = {nullptr, nullptr, nullptr};

static void MakeDock(int i, ChatDock::Kind k, const char* id, const char* titleKey)
{
    auto d = new ChatDock(k);
    d->setObjectName(id);
    if (!obs_frontend_add_dock_by_id(id, obs_module_text(titleKey), d)) {
        delete d;
        return;
    }
    g_docks[i] = d;
}

// Les docks kick et YouTube prennent la taille du dock Twitch (reference).
static void ApplyReferenceSize()
{
    ChatDock* ref = g_docks[0];
    if (!ref) return;
    auto rdw = qobject_cast<QDockWidget*>(ref->parentWidget());
    if (!rdw || !rdw->isVisible()) return;
    QSize sz = rdw->size();
    auto mw = static_cast<QMainWindow*>(obs_frontend_get_main_window());
    for (int i = 1; i < 3; i++) {
        if (!g_docks[i]) continue;
        auto dw = qobject_cast<QDockWidget*>(g_docks[i]->parentWidget());
        if (!dw || !dw->isVisible()) continue;
        if (dw->isFloating()) {
            dw->resize(sz);
        } else if (mw) {
            mw->resizeDocks({rdw, dw}, {sz.width(), sz.width()}, Qt::Horizontal);
            mw->resizeDocks({rdw, dw}, {sz.height(), sz.height()}, Qt::Vertical);
        }
    }
}

void CreateChatDocks()
{
    MakeDock(0, ChatDock::Twitch, "obslink-chat-twitch", "Chat.Twitch.Title");
    MakeDock(1, ChatDock::Kick, "obslink-chat-kick", "Chat.Kick.Title");
    MakeDock(2, ChatDock::YouTube, "obslink-chat-youtube", "Chat.YouTube.Title");
}

void OnChatDocksFinishedLoading()
{
    if (g_loaded) return;
    g_loaded = true;
    g_cef = obs_browser_init_panel();
    if (g_cef) g_cef->init_browser();
    else blog(LOG_WARNING, TAG "navigateur integre d'OBS indisponible : docks de chat inactifs");
    for (auto d : g_docks)
        if (d) d->LoadSaved();
    // memorise la taille actuelle des trois docks (disposition restauree par OBS)
    QTimer::singleShot(3000, []() { for (auto d : g_docks) if (d) d->SaveSizeNow(); });
}

void OnChatDocksExit()
{
    for (auto d : g_docks) if (d) d->SaveSizeNow();
}

} // namespace obslink

#include "chat-docks.moc"
