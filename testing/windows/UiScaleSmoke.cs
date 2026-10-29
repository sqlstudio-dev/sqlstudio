// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
using System;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;
using Aga.Controls.Tree;
using MySQL;
using MySQL.Controls;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]

internal static class UiScaleSmoke
{
  private static void Check(bool ok, string text) { if (!ok) throw new Exception(text); }
  private static void Near(float actual, float expected, string text) { Check(Math.Abs(actual - expected) < 0.1f, text + ": " + actual + " vs " + expected); }
  private static void SetDpi(Control control, int dpi)
  {
    // Simulate the framework's per-monitor DeviceDpi update, without changing
    // Windows settings. Hardware monitor transitions remain a manual check.
    var field = typeof(Control).GetField("deviceDpi", BindingFlags.Instance | BindingFlags.NonPublic);
    Check(field != null, ".NET Framework deviceDpi field available for metric tests");
    field.SetValue(control, dpi);
    foreach (Control child in control.Controls) SetDpi(child, dpi);
  }
  [STAThread]
  private static int Main()
  {
    try
    {
      Application.EnableVisualStyles();
      MySQL.SqlStudio.Logger.InitLogger(AppDomain.CurrentDomain.BaseDirectory);
      UiScale.Configure(100);
      using (var form = new Form { Size = new Size(1000, 700) })
      using (var menu = new MenuStrip())
      using (var tabs = new FlatTabControl { Top = 40, Size = new Size(900, 550) })
      using (var page = new TabPage("Columns and properties"))
      using (var field = new TextBox { Location = new Point(20, 20), Size = new Size(200, 24) })
      using (var tree = new TreeViewAdv { Location = new Point(20, 60), Size = new Size(400, 200), UseColumns = true })
      using (var grid = new DataGridView { Location = new Point(450, 60), Size = new Size(400, 200) })
      {
        float oldMenu = menu.Font.SizeInPoints;
        var file = (ToolStripMenuItem)menu.Items.Add("&File");
        file.DropDownItems.Add("Open a long translated document name");
        tree.Columns.Add(new TreeColumn("Column name", 120));
        grid.Columns.Add("name", "Column name");
        grid.Rows.Add("unchanged");
        tabs.TabPages.Add(page);
        page.Controls.Add(field); page.Controls.Add(tree); page.Controls.Add(grid);
        form.Controls.Add(tabs); form.Controls.Add(menu);
        UiScale.Attach(form);
        UiScale.Apply(form);
        Near(menu.Font.SizeInPoints, oldMenu, "Main menu uses the system-sized baseline");
        Check(field.Font.SizeInPoints >= menu.Font.SizeInPoints && field.Font.SizeInPoints <= menu.Font.SizeInPoints + 1f, "Primary text stays at compact menu proportions");
        Check(tree.RowHeight >= tree.Font.Height + 4, "Tree rows fit enlarged text");
        Check(tabs.ItemSize.Height > tabs.Font.Height, "Tab header fits text");
        UiScale.Attach(file.DropDown); UiScale.Apply(file.DropDown);
        Near(file.DropDown.Font.SizeInPoints, menu.Font.SizeInPoints, "Dropdown font does not compound inherited scaling");
        Near(file.DropDownItems[0].Font.SizeInPoints, menu.Font.SizeInPoints, "Dropdown item font");
        int originalDpi = tree.DeviceDpi;
        var bounds = field.Bounds; var rowHeight = tree.RowHeight; var tabSize = tabs.ItemSize;
        for (int i = 0; i < 5; ++i) { UiScale.Apply(form); ControlTheme.Attach(form); }
        Check(field.Bounds == bounds && tree.RowHeight == rowHeight && tabs.ItemSize == tabSize, "Repeated attachment/layout must not compound zoom");
        using (var late = new TextBox())
        {
          page.Controls.Add(late); UiScale.Apply(form);
          Near(late.Font.SizeInPoints, field.Font.SizeInPoints, "Late field inherits the original font baseline");
          var size = late.Size;
          form.Controls.Add(late); UiScale.Apply(form);
          page.Controls.Add(late); UiScale.Apply(form);
          Check(late.Size == size, "Reparenting does not compound geometry: " + size + " -> " + late.Size);
        }
        foreach (int dpi in new[] { 96, 120, 144, 168, 192, originalDpi })
        {
          SetDpi(form, dpi); UiScale.Apply(form);
          Near(menu.Font.SizeInPoints, oldMenu, "DPI does not multiply point font sizes");
          Check(tree.RowHeight >= UiScale.Pixels(16, dpi), "Tree metric at DPI " + dpi + ", DeviceDpi=" + tree.DeviceDpi + ", row=" + tree.RowHeight);
          Check(tabs.ItemSize.Height > UiScale.Pixels(20, dpi), "Tab metric at DPI " + dpi);
          Near(tree.GlyphScale, dpi / 96f, "Tree glyph scale at DPI " + dpi);
        }
        Check(tree.RowHeight == rowHeight && tabs.ItemSize == tabSize, "DPI round trip restores metrics: row " + rowHeight + " -> " + tree.RowHeight + ", tabs " + tabSize + " -> " + tabs.ItemSize);
        UiScale.Configure(150); UiScale.Apply(form);
        Near(menu.Font.SizeInPoints, oldMenu * 1.5f, "Additional zoom combines with typography");
        UiScale.Configure(100); UiScale.Apply(form);
        Near(menu.Font.SizeInPoints, oldMenu, "Zoom round trip preserves baseline");
        Check((string)grid[0, 0].Value == "unchanged", "Grid content preserved");
      }
      Console.WriteLine("PASS: compact system menu and primary text, dropdown inheritance, rows, tabs, late controls, reparenting, idempotence, zoom and 96/120/144/168/192 DPI metric round trips.");
      return 0;
    }
    catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
  }
}
