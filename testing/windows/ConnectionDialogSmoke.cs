// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
using System;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;
using MySQL;
using MySQL.Controls;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]
internal static class ConnectionDialogSmoke
{
  // LayoutBox normally paints through its native backend. This offline fixture
  // keeps the real layout engine and supplies only the missing background paint.
  sealed class FixtureBox : MySQL.Forms.LayoutBox {
    protected override void OnPaintBackground(PaintEventArgs e) { e.Graphics.Clear(BackColor); }
  }
  [System.Runtime.InteropServices.DllImport("dwmapi.dll")]
  static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
  static Assembly bridge;
  static int failures;
  static Control Native(string name) {
    foreach (var type in bridge.GetTypes()) if (type.Name == name) return (Control)Activator.CreateInstance(type, true);
    throw new Exception("Missing native bridge control: " + name);
  }
  static void Check(bool ok, string message) { if (!ok) { ++failures; Console.Error.WriteLine("FAIL: " + message); } }
  static void Set(Control c, string name, object value) { c.GetType().GetProperty(name).SetValue(c, value, null); }
  static void Add(Control box, Control child, bool expand, bool fill) { box.GetType().GetMethod("Add").Invoke(box, new object[] { child, expand, fill }); }
  static Label Label(string text) { var c = (Label)Native("WrapControlLabel"); c.Text = text; c.Size = new Size(170, 13); return c; }
  static Button Button(string text) { var c = (Button)Native("MformsButton"); c.FlatStyle = FlatStyle.System; c.Text = text; c.Padding = new Padding(10, 0, 10, 0); c.Size = new Size(TextRenderer.MeasureText(text, c.Font).Width, 23); return c; }
  [STAThread] static int Main() {
    try {
      Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
      Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
      MySQL.SqlStudio.Logger.InitLogger(AppDomain.CurrentDomain.BaseDirectory);
      bridge = Assembly.LoadFrom(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "mforms.wr.dll"));
      using (var form = new Form { Text = "Setup New Connection", ClientSize = new Size(1000, 540), ShowInTaskbar = false, StartPosition = FormStartPosition.Manual, Location = new Point(-20000, -20000) }) {
        var top = Native("Table"); Set(top, "ColumnCount", 3); Set(top, "RowCount", 2); Set(top, "RowSpacing", 4); Set(top, "ColumnSpacing", 8);
        top.Dock = DockStyle.Top; top.Height = 80;
        var name = (TextBox)Native("MformsTextBox"); name.AutoSize = false; name.Size = new Size(300, 23);
        var caption = Label("Connection Name:");
        var method = Label("Connection Method:");
        var selector = new ComboBox { Text = "Standard (TCP/IP)", DropDownStyle = ComboBoxStyle.DropDownList }; selector.Items.Add("Standard (TCP/IP)"); selector.SelectedIndex = 0;
        var add = top.GetType().GetMethod("Add");
        add.Invoke(top, new object[] {caption, 0, 1, 0, 1, 8});
        add.Invoke(top, new object[] {name, 1, 2, 0, 1, 14});
        var description = Label("Type a name for the connection");
        description.MinimumSize = new Size(0, 48); // A wrapped help row must not stretch the single-line field.
        add.Invoke(top, new object[] {description, 2, 3, 0, 1, 8});
        add.Invoke(top, new object[] {method, 0, 1, 1, 2, 8});
        add.Invoke(top, new object[] {selector, 1, 2, 1, 2, 14});
        add.Invoke(top, new object[] {Label("Method to use to connect to the RDBMS"), 2, 3, 1, 2, 8});
        var tabs = (TabControl)Native("MformsStandardTabControl"); tabs.Dock = DockStyle.Fill;
        foreach (string title in new[] {"Parameters", "SSL", "Advanced"}) tabs.TabPages.Add(title);
        tabs.TabPages[0].Controls.Add(new TextBox { Text = "127.0.0.1", Location = new Point(130, 20), Width = 260 });
        tabs.TabPages[0].Controls.Add(new Label { Text = "Hostname:", Location = new Point(20, 22), AutoSize = true });
        var bar = new FixtureBox(); Set(bar, "Horizontal", true); Set(bar, "Spacing", 12); bar.Dock = DockStyle.Bottom; bar.Height = 40;
        var configure = Button("Configure Server Management..."); var test = Button("Test Connection"); var cancel = Button("Cancel"); var ok = Button("OK");
        Add(bar, configure, false, true); Add(bar, test, false, true); Add(bar, cancel, false, true); Add(bar, ok, false, true);
        form.Controls.Add(tabs); form.Controls.Add(top); form.Controls.Add(bar);
        ControlTheme.Attach(form); Conversions.SetColorScheme(ColorScheme.ColorSchemeDark);
        form.Show(); Application.DoEvents(); UiScale.Apply(form); Application.DoEvents();
        test.Text = "Test Connection to MySQL 8.0";
        test.Font = new Font(test.Font.FontFamily, test.Font.SizeInPoints + 2);
        Application.DoEvents();
        foreach (var scheme in new[] {ColorScheme.ColorSchemeDark, ColorScheme.ColorSchemeStandardWin8, ColorScheme.ColorSchemeDark}) {
          Conversions.SetColorScheme(scheme); Application.DoEvents(); UiScale.Apply(form);
          bool dark = scheme == ColorScheme.ColorSchemeDark;
          int darkCaption;
          if (DwmGetWindowAttribute(form.Handle, 20, out darkCaption, sizeof(int)) == 0)
            Check(darkCaption == (dark ? 1 : 0), "Dialog title bar follows the current theme");
          name.Text = ""; name.GetType().GetMethod("OnGotFocus", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic).Invoke(name, new object[] {EventArgs.Empty});
          name.Text = "MySQL 8.0";
          Check(name.Height <= Math.Max(name.PreferredHeight, name.MinimumSize.Height), "Single-line fields keep their preferred height beside taller help text: height=" + name.Height + ", preferred=" + name.PreferredHeight + ", minimum=" + name.MinimumSize + ", maximum=" + name.MaximumSize);
          Check(!dark || name.ForeColor.GetBrightness() > 0.5f, "Focused connection name stays readable");
          foreach (Control c in new Control[] {caption, method}) Check(c.Height >= c.GetPreferredSize(Size.Empty).Height, "Label fits: " + c.Text);
          foreach (Button c in new[] {configure, test, cancel, ok}) {
            int width = TextRenderer.MeasureText(c.Text, c.Font).Width + c.Padding.Horizontal;
            Check(c.Width >= width, "Complete button caption fits: " + c.Text + " (" + c.Width + " < " + width + ")");
          }
          for (int i = 1; i < bar.Controls.Count; ++i)
            Check(bar.Controls[i - 1].Right <= bar.Controls[i].Left, "Button captions remain separated after layout");
          Check(tabs is FlatTabControl, "Standard dialog tabs use the shared theme renderer");
          Check(!dark || tabs.TabPages[0].BackColor.GetBrightness() < 0.3f, "Tab content follows dark palette");
          tabs.SelectedIndex = 1; Check(tabs.SelectedTab.Text == "SSL", "SSL tab remains selectable"); tabs.SelectedIndex = 0;
          using (var bitmap = new Bitmap(form.Width, form.Height)) { form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size)); bitmap.Save(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "connection-dialog-" + (dark ? "dark" : "light") + ".png")); }
        }
      }
      if (failures != 0) throw new Exception(failures + " connection dialog checks failed");
      Console.WriteLine("PASS: native connection field focus colors, table label sizing, button captions, themed tabs and light/dark switching."); return 0;
    } catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
  }
}
