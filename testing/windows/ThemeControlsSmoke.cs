// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
// Run with RunThemeControlsSmoke.ps1 after building the Windows application.
using System;
using System.Windows.Forms;
using MySQL;
using MySQL.Controls;
using MySQL.Utilities;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]

internal static class ThemeControlsSmoke
{
  private sealed class TestGrid : GridView
  {
    public TestGrid() : base(null) { }
    public void RecreateWindow() { RecreateHandle(); }
  }

  private sealed class TestTextBox : TextBox
  {
    public void RecreateWindow() { RecreateHandle(); }
  }

  private static void Check(bool condition, string message)
  {
    if (!condition) throw new Exception(message);
  }

  private static void SetTheme(ColorScheme scheme)
  {
    Conversions.SetColorScheme(scheme);
    Application.DoEvents(); // HWND style updates run after notification dispatch.
  }

  private static void CreateHandles(Control control)
  {
    IntPtr handle = control.Handle;
    foreach (Control child in control.Controls) CreateHandles(child);
  }

  private static void CheckNativeButtonsOnDocumentShow()
  {
    foreach (ButtonBase button in new ButtonBase[] { new Button(), new CheckBox(), new RadioButton() })
    using (button)
    using (var host = new Form { ShowInTaskbar = false, StartPosition = FormStartPosition.Manual,
      Location = new System.Drawing.Point(-20000, -20000) })
    using (var document = new TabDocument())
    {
      SetTheme(ColorScheme.ColorSchemeDark);
      button.FlatStyle = FlatStyle.System; // Apply/Revert in the live Table Editor use this style.
      button.Text = "Apply";
      document.Controls.Add(button);
      host.Controls.Add(document);
      host.Show();
      document.Show();
      Application.DoEvents();
      Check(button.IsHandleCreated && button.FlatStyle == FlatStyle.Flat, "Native button is themed after document.Show: " + button.GetType().Name);
      IntPtr oldHandle = button.Handle;
      Conversions.SetColorScheme(ColorScheme.ColorSchemeStandardWin8);
      Check(button.Handle == oldHandle, "Theme notification must not destroy native button HWNDs during observer dispatch");
      Application.DoEvents();
      Check(button.FlatStyle == FlatStyle.System, "Native button style restored");
      SetTheme(ColorScheme.ColorSchemeDark);
      Application.DoEvents();
      Check(button.IsHandleCreated && button.FlatStyle == FlatStyle.Flat, "Native button survives theme-driven handle recreation");
      // Closing a document before its queued style update must be safe too.
      Conversions.SetColorScheme(ColorScheme.ColorSchemeStandardWin8);
      document.Dispose();
      Application.DoEvents();
    }
  }

  private static void CheckTableEditorLayout()
  {
    var assembly = System.Reflection.Assembly.LoadFrom(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "db.mysql.editors.wbp.fe.dll"));
    var type = assembly.GetType("MySQL.GUI.SqlStudio.Plugins.DbMysqlTableEditor", true);
    using (var editor = (Form)Activator.CreateInstance(type))
    {
      // Exercise the real designer hierarchy without creating a backend or connecting to a database.
      type.GetMethod("InitializeComponent", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(editor, null);
      var designSize = editor.ClientSize;
      ControlTheme.Attach(editor);
      foreach (string eventName in new[] { "Load", "Shown" })
      {
        var handler = type.GetMethod("DbMysqlTableEditor_" + eventName, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        type.GetEvent(eventName).RemoveEventHandler(editor, Delegate.CreateDelegate(typeof(EventHandler), editor, handler));
      }
      editor.TopLevel = true;
      editor.ShowInTaskbar = false;
      editor.StartPosition = FormStartPosition.Manual;
      editor.Location = new System.Drawing.Point(-20000, -20000);
      editor.Show();
      CreateHandles(editor);
      Application.DoEvents();
      Check(editor.Width < designSize.Width * UiScale.LayoutScale * editor.DeviceDpi / 96f * 1.2f, "Layout must not fit content before its host receives application zoom");
      var stableSize = editor.ClientSize;
      for (int pass = 0; pass < 4; ++pass) UiScale.Apply(editor);
      Check(editor.ClientSize == stableSize, "Preferred layout fitting must not inflate dialogs on repeated passes");
      foreach (var scheme in new[] { ColorScheme.ColorSchemeDark, ColorScheme.ColorSchemeStandardWin8, ColorScheme.ColorSchemeDark })
      {
        SetTheme(scheme);
        bool dark = scheme == ColorScheme.ColorSchemeDark;
        var nameField = editor.Controls.Find("nameTextBox", true)[0];
        var schemaCaption = editor.Controls.Find("label4", true)[0];
        Check(nameField.Right <= schemaCaption.Left, "Table name must not overlap schema caption");
        var autoIncrement = editor.Controls.Find("aiCheckBox", true)[0];
        int textWidth = TextRenderer.MeasureText(autoIncrement.Text, autoIncrement.Font, System.Drawing.Size.Empty, TextFormatFlags.NoPadding | TextFormatFlags.SingleLine).Width;
        Check(autoIncrement.Width >= textWidth + UiScale.Pixels(13 + 4, editor.DeviceDpi), "Checkbox text fits beside enlarged glyph");
        foreach (string name in new[] { "nameTextBox", "optComments", "optEngine", "optCharset", "optCollation", "headingLayoutPanel", "columnCharsetCollationPanel", "columnCommentTextBox", "columnsTreeView" })
        {
          var matches = editor.Controls.Find(name, true);
          Check(matches.Length == 1, "Table Editor control exists: " + name);
          Check((matches[0].BackColor.GetBrightness() < 0.25f) == dark, "Table Editor background follows theme: " + name);
          Check(!dark || matches[0].ForeColor.GetBrightness() > 0.5f, "Table Editor foreground readable: " + name);
        }
        using (var bitmap = new System.Drawing.Bitmap(editor.Width, editor.Height))
        {
          editor.DrawToBitmap(bitmap, editor.ClientRectangle);
          bitmap.Save(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "theme-table-editor-" + (dark ? "dark" : "light") + ".png"));
        }
      }
      using (var host = new Form { ClientSize = new System.Drawing.Size(900, 600), ShowInTaskbar = false,
        StartPosition = FormStartPosition.Manual, Location = new System.Drawing.Point(-20000, -20000) })
      using (var document = new UserControl { Dock = DockStyle.Fill })
      {
        // Reparenting changes focus; this fixture has no database backend.
        var field = editor.Controls.Find("nameTextBox", true)[0];
        var leave = type.GetMethod("nameTextBox_Leave", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
        field.Leave -= (EventHandler)Delegate.CreateDelegate(typeof(EventHandler), editor, leave);
        UiScale.AttachApplication(host);
        UiScale.SetDocumentMinimumSize(document, designSize);
        while (editor.Controls.Count > 0) editor.Controls[0].Parent = document;
        host.Controls.Add(document);
        host.Show(); Application.DoEvents(); UiScale.Apply(host);
        Check(document.AutoScroll && document.AutoScrollMinSize.Width > document.ClientSize.Width, "Docked editor exposes horizontal scrolling");
        var scrollSize = document.AutoScrollMinSize;
        for (int pass = 0; pass < 4; ++pass) UiScale.Apply(host);
        Check(document.AutoScrollMinSize == scrollSize, "Document minimum size does not compound after reparenting");
        host.ClientSize = new System.Drawing.Size(700, 500);
        Application.DoEvents(); UiScale.Apply(host);
        var nameField = document.Controls.Find("nameTextBox", true)[0];
        var schemaCaption = document.Controls.Find("label4", true)[0];
        Check(nameField.Right <= schemaCaption.Left, "Narrow docked editor scrolls instead of overlapping fields");
      }
    }
  }

  [STAThread]
  private static int Main()
  {
    try
    {
      if (SystemInformation.HighContrast)
        throw new Exception("Run the light/dark control checks with Windows High Contrast disabled.");
      Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
      Application.EnableVisualStyles();
      string logDirectory = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "theme-test-logs");
      System.IO.Directory.CreateDirectory(logDirectory);
      MySQL.SqlStudio.Logger.InitLogger(logDirectory);
      CheckNativeButtonsOnDocumentShow();
      SetTheme(ColorScheme.ColorSchemeDark);
      using (var grid = new TestGrid())
      using (var find = new FindPanel())
      {
        grid.CreateControl();
        find.CreateControl();
        Check(grid.IsHandleCreated && find.IsHandleCreated, "Controls must subscribe to live theme notifications.");
        var search = (TextBox)find.Controls.Find("searchTextBox", true)[0];
        search.Text = "SELECT retained text";
        search.Select(7, 8);
        grid.DefaultCellStyle.WrapMode = DataGridViewTriState.True;
        grid.ColumnHeadersDefaultCellStyle.Alignment = DataGridViewContentAlignment.MiddleRight;
        Check(grid.BackgroundColor.GetBrightness() < 0.2f, "Dark empty grid background");
        Check(grid.DefaultCellStyle.ForeColor.GetBrightness() > 0.7f, "Dark cell text");
        Check(grid.AlternatingRowsDefaultCellStyle.BackColor.GetBrightness() < 0.2f, "Dark alternating rows");
        Check(!grid.EnableHeadersVisualStyles, "Dark headers must use the palette");
        Check(search.BackColor.GetBrightness() < 0.2f, "Dark search field");

        SetTheme(ColorScheme.ColorSchemeStandardWin8);
        Check(grid.BackgroundColor == System.Drawing.SystemColors.Window, "Live light grid background");
        Check(grid.DefaultCellStyle.ForeColor == System.Drawing.SystemColors.WindowText, "Live light grid text");
        Check(grid.DefaultCellStyle.SelectionBackColor == System.Drawing.SystemColors.Highlight, "Light selection restored");
        Check(grid.EnableHeadersVisualStyles, "Light native headers restored");
        Check(search.BackColor == System.Drawing.SystemColors.Window, "Live light search field");

        grid.RecreateWindow();
        SetTheme(ColorScheme.ColorSchemeDark);
        Check(grid.BackgroundColor.GetBrightness() < 0.2f, "Notifications survive handle recreation");
        Check(search.Text == "SELECT retained text" && search.SelectionStart == 7 && search.SelectionLength == 8,
          "Theme changes must preserve search text and selection");
        Check(grid.DefaultCellStyle.WrapMode == DataGridViewTriState.True, "Theme changes must preserve wrapping");
        Check(grid.ColumnHeadersDefaultCellStyle.Alignment == DataGridViewContentAlignment.MiddleRight,
          "Theme changes must preserve header alignment");
      }
      using (var document = new TabDocument())
      using (var text = new TestTextBox())
      using (var combo = new ComboBox())
      using (var tree = new Aga.Controls.Tree.TreeViewAdv())
      {
        document.Controls.Add(text);
        document.Controls.Add(combo);
        document.Controls.Add(tree);
        tree.Size = new System.Drawing.Size(300, 150);
        tree.UseColumns = true;
        tree.NodeControls.Add(new Aga.Controls.Tree.NodeControls.NodeTextBox { DataPropertyName = "Text" });
        var model = new Aga.Controls.Tree.TreeModel();
        model.Nodes.Add(new Aga.Controls.Tree.Node("VARCHAR"));
        tree.Model = model;
        tree.Columns.Add(new Aga.Controls.Tree.TreeColumn("Type", 100));
        tree.NodeControls[0].ParentColumn = tree.Columns[0];
        document.CreateControl();
        // Explicitly create handles: TabDocument is an embedded, initially hidden form.
        IntPtr textHandle = text.Handle, comboHandle = combo.Handle, treeHandle = tree.Handle;
        text.Text = "Uncommitted description";
        text.Select(3, 5);
        text.ReadOnly = true;
        combo.Items.Add("Selected object");
        combo.SelectedIndex = 0;
        SetTheme(ColorScheme.ColorSchemeDark);
        Check(text.BackColor.GetBrightness() < 0.2f, "Dark read-only description");
        Check(combo.BackColor.GetBrightness() < 0.2f && combo.FlatStyle == FlatStyle.Flat, "Dark selector");
        Check(tree.BackColor.GetBrightness() < 0.2f && tree.HeaderBackColor.GetBrightness() < 0.2f, "Dark tree and header");
        tree.FullRowSelect = true;
        tree.SelectedNode = tree.Root.Children[0];
        using (var bitmap = new System.Drawing.Bitmap(tree.Width, tree.Height))
        {
          tree.DrawToBitmap(bitmap, tree.ClientRectangle);
          Check(bitmap.GetPixel(250, 5).GetBrightness() < 0.25f, "Rendered header remainder is dark");
          Check(bitmap.GetPixel(250, 100).GetBrightness() < 0.2f, "Rendered empty tree is dark");
          Check(bitmap.GetPixel(250, tree.ColumnHeaderHeight + 5).GetBrightness() < 0.3f, "Rendered selected tree row is dark");
          bitmap.Save(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "theme-tree-dark.png"));
        }
        text.RecreateWindow();
        Check(text.BackColor.GetBrightness() < 0.2f, "Description theme survives handle recreation");
        using (var lateField = new TextBox())
        {
          document.Controls.Add(lateField);
          IntPtr lateHandle = lateField.Handle;
          Check(lateField.BackColor.GetBrightness() < 0.2f, "Late-added field is themed");
        }
        SetTheme(ColorScheme.ColorSchemeStandardWin8);
        Check(text.BackColor == System.Drawing.SystemColors.Window, "Description restores light palette");
        Check(tree.HeaderBackColor.IsEmpty, "Tree restores native header rendering");
        Check(combo.FlatStyle == FlatStyle.Standard && combo.SelectedIndex == 0, "Selector style and selection restored");
        Check(text.Text == "Uncommitted description" && text.SelectionStart == 3 && text.SelectionLength == 5 && text.ReadOnly,
          "Description content, selection and read-only state survive switching");
      }
      using (var page = new Panel { BackColor = System.Drawing.Color.White, Size = new System.Drawing.Size(500, 300) })
      using (var table = new Panel { Dock = DockStyle.Fill })
      using (var label = new Label { Text = "Available Server Features", ForeColor = System.Drawing.Color.Black, AutoSize = true })
      using (var button = new Button { Text = "Refresh", Top = 40 })
      using (var status = new Label { Text = "Stopped", ForeColor = System.Drawing.Color.Red, Top = 80 })
      {
        page.Controls.Add(table);
        table.Controls.Add(label);
        table.Controls.Add(button);
        table.Controls.Add(status);
        ControlTheme.Attach(page);
        page.CreateControl();
        SetTheme(ColorScheme.ColorSchemeDark);
        Check(page.BackColor.GetBrightness() < 0.2f && table.BackColor.GetBrightness() < 0.2f, "Administration page and nested layout are dark");
        Check(label.ForeColor.GetBrightness() > 0.7f, "Administration label is readable");
        Check(button.BackColor.GetBrightness() < 0.25f && button.FlatStyle == FlatStyle.Flat, "Administration button is dark");
        Check(status.ForeColor == System.Drawing.Color.Red, "Semantic status color is preserved");
        page.BackColor = System.Drawing.Color.White;
        label.ForeColor = System.Drawing.Color.Black;
        Check(page.BackColor.GetBrightness() < 0.2f && label.ForeColor.GetBrightness() > 0.7f,
          "Late page refresh cannot restore white background or black text");
        using (var latePage = new Panel { BackColor = page.BackColor })
        {
          page.Controls.Add(latePage);
          latePage.CreateControl();
          SetTheme(ColorScheme.ColorSchemeStandardWin8);
          Check(latePage.BackColor.GetBrightness() > 0.8f, "Page created in dark mode returns to light");
        }
        Check(page.BackColor == System.Drawing.Color.White && label.ForeColor == System.Drawing.SystemColors.ControlText, "Administration restores light colors");
        Check(button.FlatStyle == FlatStyle.Standard, "Administration restores native button style");
        SetTheme(ColorScheme.ColorSchemeStandard);
        Check((page.BackColor.GetBrightness() < 0.2f) == Conversions.InDarkMode(), "Administration follows resolved System palette");
        SetTheme(ColorScheme.ColorSchemeDark);
        using (var bitmap = new System.Drawing.Bitmap(page.Width, page.Height))
        {
          page.DrawToBitmap(bitmap, page.ClientRectangle);
          Check(bitmap.GetPixel(450, 250).GetBrightness() < 0.2f, "Rendered administration layout is dark");
          bitmap.Save(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "theme-admin-dark.png"));
        }
      }
      using (var host = new Panel { Size = new System.Drawing.Size(700, 500) })
      using (var tabs = new FlatTabControl { Dock = DockStyle.Fill })
      using (var page = new TabPage("Columns") { BackColor = System.Drawing.Color.White })
      using (var split = new SplitContainer { Dock = DockStyle.Fill })
      using (var grid = new DataGridView { Dock = DockStyle.Fill })
      using (var strip = new ToolStrip())
      using (var apply = new Button { Text = "Apply" })
      using (var check = new CheckBox { Text = "Primary key" })
      {
        host.Controls.Add(tabs);
        tabs.TabPages.Add(page);
        page.Controls.Add(split);
        split.Panel1.Controls.Add(grid);
        split.Panel2.Controls.Add(strip);
        split.Panel2.Controls.Add(apply);
        split.Panel2.Controls.Add(check);
        strip.Items.Add("Revert");
        grid.Columns.Add("name", "Column name");
        grid.Rows.Add("retained_name");
        grid.DefaultCellStyle.WrapMode = DataGridViewTriState.True;
        ControlTheme.Attach(host);
        host.CreateControl();
        IntPtr handle = grid.Handle, buttonHandle = apply.Handle, checkHandle = check.Handle, stripHandle = strip.Handle;
        for (int i = 0; i < 3; ++i)
        {
          SetTheme(ColorScheme.ColorSchemeDark);
          Check(page.BackColor.GetBrightness() < 0.2f && split.Panel2.BackColor.GetBrightness() < 0.2f, "Nested Table Editor surfaces are dark");
          Check(split.BackColor != split.Panel2.BackColor, "Splitter is distinct from its panels");
          Check(grid.BackgroundColor.GetBrightness() < 0.2f && !grid.EnableHeadersVisualStyles, "Standard grid uses dark cells and headers");
          Check(grid.DefaultCellStyle.SelectionBackColor == Conversions.GetApplicationColor(ApplicationColor.AppColorSelection, false), "Grid selection uses the shared palette");
          Check(check.FlatStyle == FlatStyle.Flat && apply.FlatStyle == FlatStyle.Flat, "Checkbox and button follow theme");
          apply.Enabled = false;
          Check(apply.ForeColor == Conversions.GetApplicationColor(ApplicationColor.AppColorDisabled, true), "Disabled button follows palette");
          apply.Enabled = true;
          Check(apply.FlatAppearance.MouseOverBackColor != apply.FlatAppearance.MouseDownBackColor, "Button hover and pressed states are distinct");
          SetTheme(ColorScheme.ColorSchemeStandardWin8);
          Check(page.BackColor.GetBrightness() > 0.8f, "Nested tab returns to light");
          Check(apply.FlatStyle == FlatStyle.Standard && check.FlatStyle == FlatStyle.Standard, "Native button and checkbox styles restored");
          Check(grid.EnableHeadersVisualStyles, "Native grid headers restored");
          Check(grid.DefaultCellStyle.WrapMode == DataGridViewTriState.True && (string)grid[0, 0].Value == "retained_name", "Grid content and layout survive repeated switching");
        }
      }
      SetTheme(ColorScheme.ColorSchemeDark);
      SetTheme(ColorScheme.ColorSchemeStandardWin8);
      CheckTableEditorLayout();
      Console.WriteLine("PASS: native System-style buttons on document.Show, deferred HWND changes, real Table Editor layout/reparenting/scrolling, administration surfaces, labels, buttons, System palette, late updates, grids, fields, rendering, state preservation, handle recreation, and disposal.");
      return 0;
    }
    catch (Exception error)
    {
      Console.Error.WriteLine(error);
      return 1;
    }
  }
}

