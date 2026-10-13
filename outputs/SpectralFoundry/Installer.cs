// Spectral Foundry installer. AGPL-3.0; see LICENSE.txt.
// Build: csc /target:winexe /platform:x64 /reference:System.IO.Compression.dll
// /reference:System.IO.Compression.FileSystem.dll /reference:System.Windows.Forms.dll
// /reference:System.Drawing.dll /resource:installer-payload.zip,Payload
// /out:SpectralFoundry-Setup.exe Installer.cs
using System;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Windows.Forms;

static class Setup
{
    const string Bundle = "Spectral Foundry.vst3";
    static string Digest(string path) {
        using (var hash = SHA256.Create()) using (var stream = File.OpenRead(path))
            return BitConverter.ToString(hash.ComputeHash(stream));
    }
    static void CopyTree(string source, string destination) {
        Directory.CreateDirectory(destination);
        foreach (string file in Directory.GetFiles(source)) File.Copy(file, Path.Combine(destination, Path.GetFileName(file)), false);
        foreach (string folder in Directory.GetDirectories(source)) {
            if ((File.GetAttributes(folder) & FileAttributes.ReparsePoint) != 0) throw new IOException("Unexpected linked folder in the existing installation.");
            CopyTree(folder, Path.Combine(destination, Path.GetFileName(folder)));
        }
    }
    static string Install(string root, string backupRoot = null) {
        root = Environment.ExpandEnvironmentVariables((root ?? "").Trim().Trim('"'));
        if (String.IsNullOrWhiteSpace(root) || !Path.IsPathRooted(root) || Path.GetPathRoot(root).Length < 3)
            throw new IOException("Enter a full folder path, for example D:\\Audio Plugins\\VST3.");
        root = Path.GetFullPath(root);
        Directory.CreateDirectory(root);
        string stage = Path.Combine(root, ".spectral-foundry-setup-" + Guid.NewGuid().ToString("N"));
        string target = Path.Combine(root, Bundle);
        Directory.CreateDirectory(stage);
        try {
            using (var resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("Payload"))
            using (var zip = new ZipArchive(resource, ZipArchiveMode.Read)) {
                foreach (var entry in zip.Entries) {
                    string path = Path.GetFullPath(Path.Combine(stage, entry.FullName));
                    if (!path.StartsWith(stage + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Invalid installer payload path.");
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    using (var input = entry.Open()) using (var output = File.Create(path)) input.CopyTo(output);
                }
            }
            string relativeBinary = @"Contents\x86_64-win\Spectral Foundry.vst3";
            string incoming = Path.Combine(stage, Bundle);
            if (!File.Exists(Path.Combine(incoming, relativeBinary))) throw new IOException("Plugin payload is incomplete.");
            if (Directory.Exists(target)) {
                string existingBinary = Path.Combine(target, relativeBinary);
                if (File.Exists(existingBinary) && Digest(existingBinary) == Digest(Path.Combine(incoming, relativeBinary)))
                    return "This version is already installed at:\n" + target;
                string metadata = Path.Combine(target, @"Contents\Resources\moduleinfo.json");
                if (!File.Exists(metadata) || !File.ReadAllText(metadata).Contains("ABCDEF019182FAEB4664727953667231"))
                    throw new IOException("This folder contains an unrecognized plugin. No existing files were changed.");
                if (backupRoot == null) backupRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), @"Spectral Foundry\Backups");
                string backup = Path.Combine(backupRoot, DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N"));
                CopyTree(target, backup);
                string previous = Path.Combine(stage, "previous");
                Directory.Move(target, previous);
                try { Directory.Move(incoming, target); }
                catch { Directory.Move(previous, target); throw; }
                // Previous files have a complete backup outside the DAW scan path.
                Directory.Delete(previous, true);
                return "Updated to Spectral Foundry 2.0.\n\n" + target + "\n\nPrevious version backed up to:\n" + backup + "\n\nIn FL Studio, rescan with Verify plugins and Rescan previously verified plugins enabled. Load preset 01 ADG - saved rack to hear the new defaults.";
            }
            Directory.Move(incoming, target);
            return "Spectral Foundry 2.0 installed.\n\n" + target + "\n\nRescan VST3 plugins in your DAW. In FL Studio use a Mixer effect slot. Select preset 01 ADG - saved rack.";
        } finally {
            // Only remove this installation's uniquely named staging directory.
            if (Directory.Exists(stage) && !Directory.Exists(Path.Combine(stage, "previous"))) Directory.Delete(stage, true);
        }
    }
    [STAThread]
    static int Main(string[] args) {
        if (args.Length == 3 && args[0] == "--test-extract") {
            try { Install(args[1], args[2]); return 0; } catch (Exception e) { File.WriteAllText(Path.Combine(Path.GetDirectoryName(args[1]), "installer-test-error.txt"), e.ToString()); return 1; }
        }
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        var form = new Form { Text = "Spectral Foundry 2.0 - Rack Tables Update", ClientSize = new Size(630, 345),
            FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false,
            StartPosition = FormStartPosition.CenterScreen, BackColor = Color.FromArgb(19, 23, 33),
            ForeColor = Color.White, Font = new Font("Segoe UI", 10) };
        var title = new Label { Text = "SPECTRAL FOUNDRY 2.0", Font = new Font("Segoe UI", 22, FontStyle.Bold),
            ForeColor = Color.FromArgb(255, 87, 156), Location = new Point(24, 22), AutoSize = true };
        var info = new Label { Text = "Original rack tables + recovered macro mappings\nClose your DAW before installing. Updates an existing copy and saves\na backup. Keep the default system VST3 folder for FL Studio.", Location = new Point(27, 78), Size = new Size(575, 74) };
        var path = new TextBox { Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonProgramFiles), "VST3"),
            Location = new Point(27, 169), Size = new Size(470, 28), ReadOnly = false };
        var browse = new Button { Text = "Browse...", Location = new Point(507, 166), Size = new Size(96, 32) };
        browse.Click += delegate { using (var dialog = new FolderBrowserDialog { Description = "Choose the VST3 folder for installation", ShowNewFolderButton = true, SelectedPath = Directory.Exists(path.Text) ? path.Text : "" })
            if (dialog.ShowDialog(form) == DialogResult.OK) path.Text = dialog.SelectedPath; };
        var note = new Label { Text = "FL Studio: keep the default system VST3 folder.\nCustom paths are available for hosts that support them.",
            ForeColor = Color.Silver, Location = new Point(27, 211), Size = new Size(570, 51) };
        var install = new Button { Text = "Install", Location = new Point(387, 286), Size = new Size(104, 35) };
        var close = new Button { Text = "Close", Location = new Point(503, 286), Size = new Size(100, 35) };
        close.Click += delegate { form.Close(); };
        install.Click += delegate {
            install.Enabled = browse.Enabled = false; form.UseWaitCursor = true;
            try { string message = Install(path.Text); MessageBox.Show(form, message, "Spectral Foundry", MessageBoxButtons.OK, MessageBoxIcon.Information); }
            catch (UnauthorizedAccessException) { MessageBox.Show(form, "Windows denied access to that folder. Choose a writable folder, or close this installer and use Run as administrator for a protected location such as Program Files.", "Folder access", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            catch (Exception e) { MessageBox.Show(form, e.Message, "Installation could not finish", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            finally { install.Enabled = browse.Enabled = true; form.UseWaitCursor = false; }
        };
        form.Controls.AddRange(new Control[] { title, info, path, browse, note, install, close });
        form.AcceptButton = install; form.CancelButton = close;
        Application.Run(form);
        return 0;
    }
}
