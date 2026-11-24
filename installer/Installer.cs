// Installateur OBSLink : retire « Sortie multiple » (obs-multi-rtmp) s'il est présent, puis installe OBSLink.
// Le plugin est embarqué dans l'exécutable (ressource « payload.zip »). Aucune connexion Internet.
// Options : /silencieux (sans fenêtre de confirmation)   /desinstaller (retire OBSLink)
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

static class Installer
{
    const string Titre = "Installation d'OBSLink";
    static readonly string ProgramData = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
    static readonly string ProgramFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
    static readonly string AppData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
    static StringBuilder log = new StringBuilder();
    static bool silencieux;

    [STAThread]
    static int Main(string[] args)
    {
        Application.EnableVisualStyles();
        bool desinstaller = false;
        foreach (var a in args)
        {
            if (a.Equals("/silencieux", StringComparison.OrdinalIgnoreCase)) silencieux = true;
            if (a.Equals("/desinstaller", StringComparison.OrdinalIgnoreCase)) desinstaller = true;
        }

        string obsDir = Path.Combine(ProgramFiles, "obs-studio");
        if (!File.Exists(Path.Combine(obsDir, "bin", "64bit", "obs64.exe")))
        {
            Boite("OBS Studio (64 bits) n'a pas été trouvé dans :\n" + obsDir + "\n\nInstallez OBS Studio d'abord.", MessageBoxIcon.Error);
            return 2;
        }

        string question = desinstaller
            ? "Retirer le plugin OBSLink d'OBS Studio ?"
            : "Ce programme va :\n\n • retirer le plugin « Sortie multiple » (obs-multi-rtmp) s'il est présent,\n • installer OBSLink (même fonctions, avec une coche « Son désactivé » par piste audio).\n\nVos cibles (Twitch, YouTube...) sont conservées.\n\nContinuer ?";
        if (!silencieux && MessageBox.Show(question, Titre, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
            return 1;

        // OBS ouvert ? les DLL seraient verrouillées
        while (Process.GetProcessesByName("obs64").Length > 0)
        {
            if (silencieux) { Boite("OBS est ouvert : fermez-le puis relancez.", MessageBoxIcon.Warning); return 3; }
            var r = MessageBox.Show("OBS Studio est ouvert. Fermez-le, puis cliquez sur OK pour continuer.", Titre, MessageBoxButtons.OKCancel, MessageBoxIcon.Warning);
            if (r != DialogResult.OK) return 3;
        }

        try
        {
            string sauvegarde = Path.Combine(ProgramData, "obs-studio", "plugins-sauvegarde-OBSLink", DateTime.Now.ToString("yyyy-MM-dd_HHmmss"));

            // 1) Retirer « Sortie multiple » (toutes les emplacements possibles), avec sauvegarde
            string[] anciens =
            {
                Path.Combine(ProgramData, "obs-studio", "plugins", "obs-multi-rtmp"),
                Path.Combine(AppData, "obs-studio", "plugins", "obs-multi-rtmp"),
            };
            foreach (var d in anciens)
            {
                if (!Directory.Exists(d)) continue;
                string dest = Path.Combine(sauvegarde, "obs-multi-rtmp-" + (d.StartsWith(ProgramData) ? "ProgramData" : "AppData"));
                CopieDossier(d, dest);
                Directory.Delete(d, true);
                log.AppendLine("Retiré : " + d);
            }
            // anciennes installations directes dans Program Files
            string pf64 = Path.Combine(obsDir, "obs-plugins", "64bit");
            foreach (var f in new[] { "obs-multi-rtmp.dll", "obs-multi-rtmp.pdb" })
            {
                string p = Path.Combine(pf64, f);
                if (!File.Exists(p)) continue;
                Directory.CreateDirectory(sauvegarde);
                File.Copy(p, Path.Combine(sauvegarde, f), true);
                File.Delete(p);
                log.AppendLine("Retiré : " + p);
            }
            string pfData = Path.Combine(obsDir, "data", "obs-plugins", "obs-multi-rtmp");
            if (Directory.Exists(pfData)) { CopieDossier(pfData, Path.Combine(sauvegarde, "obs-multi-rtmp-data")); Directory.Delete(pfData, true); log.AppendLine("Retiré : " + pfData); }

            // 2) Retirer une éventuelle version d'OBSLink déjà installée
            string cible = Path.Combine(ProgramData, "obs-studio", "plugins", "OBSLink");
            if (Directory.Exists(cible)) { Directory.Delete(cible, true); log.AppendLine("Ancienne version d'OBSLink remplacée."); }

            if (desinstaller)
            {
                Boite("OBSLink a été retiré.\n\n" + log, MessageBoxIcon.Information);
                return 0;
            }

            // 3) Installer OBSLink depuis la ressource embarquée
            using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
            using (var z = new ZipArchive(s, ZipArchiveMode.Read))
            {
                foreach (var e in z.Entries)
                {
                    if (e.FullName.EndsWith("/") || e.Length == 0 && e.Name == "") continue;
                    string dest = Path.Combine(cible, e.FullName.Replace('/', Path.DirectorySeparatorChar));
                    Directory.CreateDirectory(Path.GetDirectoryName(dest));
                    e.ExtractToFile(dest, true);
                }
            }
            log.AppendLine("OBSLink installé dans : " + cible);
            Boite("OBSLink est installé.\n\n" + log + "\nOuvrez OBS : le dock « OBSLink - Sortie multiple » reprend vos cibles.\nSi le dock n'apparaît pas : menu Docks > OBSLink.", MessageBoxIcon.Information);
            return 0;
        }
        catch (UnauthorizedAccessException ex)
        {
            Boite("Accès refusé : " + ex.Message + "\n\nRelancez ce programme en « Exécuter en tant qu'administrateur ».", MessageBoxIcon.Error);
            return 4;
        }
        catch (Exception ex)
        {
            Boite("Erreur : " + ex.Message, MessageBoxIcon.Error);
            return 5;
        }
    }

    static void Boite(string texte, MessageBoxIcon icone)
    {
        if (silencieux) { Console.WriteLine(texte); return; }
        MessageBox.Show(texte, Titre, MessageBoxButtons.OK, icone);
    }

    static void CopieDossier(string src, string dst)
    {
        Directory.CreateDirectory(dst);
        foreach (var f in Directory.GetFiles(src)) File.Copy(f, Path.Combine(dst, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(src)) CopieDossier(d, Path.Combine(dst, Path.GetFileName(d)));
    }
}
