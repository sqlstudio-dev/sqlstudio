// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Runtime.CompilerServices;
using System.Windows.Forms;
using Aga.Controls.Tree;

namespace MySQL.Controls
{
  // Application zoom is additional to Windows DPI. Fonts are in points; custom
  // drawing metrics are logical pixels. Never scale a subtree on ControlAdded.
  public static class UiScale
  {
    public enum FontRole { Menu, General, Navigation, Tab, Grid, Input, Secondary, Heading }
    private static readonly ConditionalWeakTable<Control, Binding> bindings = new ConditionalWeakTable<Control, Binding>();
    private static readonly ConditionalWeakTable<ToolStripItem, ItemBinding> items = new ConditionalWeakTable<ToolStripItem, ItemBinding>();
    private static float zoom = 1f;
    private static bool applying;
    public static float Zoom { get { return zoom; } }
    public static float LayoutScale { get { return zoom; } }
    public static float FontSize(FontRole role, float originalPoints)
    {
      float menu = SystemFonts.MenuFont.SizeInPoints;
      if (role == FontRole.Menu || role == FontRole.Secondary) return originalPoints * zoom;
      return Math.Max(originalPoints, menu) * zoom;
    }
    public static int Pixels(float logicalPixels, int dpi)
    {
      return (int)Math.Round(logicalPixels * zoom * dpi / 96f);
    }
    public static void Configure(int percent)
    {
      zoom = Math.Max(1f, Math.Min(2f, percent / 100f));
      Conversions.SetUiZoom(zoom);
    }
    public static void Attach(Control control)
    {
      if (control == null || control.IsDisposed || bindings.TryGetValue(control, out var existing)) return;
      bindings.Add(control, new Binding(control));
      foreach (Control child in control.Controls) Attach(child);
      Queue(control);
    }
    public static void AttachApplication(Form form)
    {
      Attach(form);
      bindings.GetValue(form, c => new Binding(c)).PreserveWindowBounds = true;
    }

    public static void SetDocumentMinimumSize(ScrollableControl document, Size designSize)
    {
      Attach(document);
      var binding = bindings.GetValue(document, c => new Binding(c));
      binding.DocumentSize = new SizeF(designSize.Width * 96f / document.DeviceDpi, designSize.Height * 96f / document.DeviceDpi);
      document.AutoScroll = true;
    }

    private static void Queue(Control control)
    {
      if (applying || control.IsDisposed || control.Disposing) return;
      Control root = control;
      while (root.Parent != null && bindings.TryGetValue(root.Parent, out var parent)) root = root.Parent;
      if (!root.IsHandleCreated || !bindings.TryGetValue(root, out var binding) || binding.Pending) return;
      binding.Pending = true;
      root.BeginInvoke(new MethodInvoker(delegate
      {
        binding.Pending = false;
        if (!root.IsDisposed && !root.Disposing && root.IsHandleCreated) Apply(root);
      }));
    }
    // Also available to layout tests and hosts that need preferred sizes before Show.
    public static void Apply(Control root)
    {
      if (applying || root == null || root.IsDisposed) return;
      Attach(root);
      var controls = new List<Control>();
      Collect(root, controls);
      applying = true;
      try
      {
        // Capture every original before changing any inherited font or parent bounds.
        foreach (var control in controls) { control.SuspendLayout(); bindings.GetValue(control, c => new Binding(c)).Capture(); }
        foreach (var control in controls) bindings.GetValue(control, c => new Binding(c)).Apply();
      }
      finally
      {
        for (int i = controls.Count - 1; i >= 0; --i) controls[i].ResumeLayout(true);
        try
        {
          foreach (var control in controls) control.PerformLayout();
          FitContent(controls);
        }
        finally { applying = false; }
      }
    }
    private static void FitContent(List<Control> controls)
    {
      // Percent columns can otherwise shrink below their children's minimum
      // widths. Grow dialogs, or provide scrolling in docked document hosts.
      foreach (var control in controls)
      {
        var table = control as TableLayoutPanel;
        if (table == null || !table.Visible || !table.AutoSize || table.Dock == DockStyle.None) continue;
        bool nestedTable = false;
        for (var parent = table.Parent; parent != null; parent = parent.Parent)
          if (parent is TableLayoutPanel) { nestedTable = true; break; }
        if (nestedTable) continue; // Its intrinsic width is included by its outer table.
        int requiredWidth = MinimumTableWidth(table) + table.Margin.Horizontal;
        for (Control host = table.Parent; host != null; host = host.Parent)
        {
          requiredWidth += host.Padding.Horizontal;
          if (!bindings.TryGetValue(host, out var binding)) continue;
          if (!binding.LayoutApplied) break;
          var form = host as Form;
          if (form != null && form.TopLevel && !binding.PreserveWindowBounds)
          {
            if (form.ClientSize.Width < requiredWidth)
              form.ClientSize = new Size(requiredWidth, form.ClientSize.Height);
            foreach (var child in controls) child.PerformLayout();
            break;
          }
          if (!binding.DocumentSize.IsEmpty)
          {
            var document = (ScrollableControl)host;
            document.AutoScrollMinSize = new Size(Math.Max(document.AutoScrollMinSize.Width, requiredWidth), document.AutoScrollMinSize.Height);
            break;
          }
        }
      }
    }
    private static int MinimumTableWidth(TableLayoutPanel table)
    {
      // GetPreferredSize on a stretched percent-column table feeds its current
      // child widths back into the next layout. Measure intrinsic minima instead.
      var widths = new float[table.ColumnCount];
      for (int i = 0; i < widths.Length; ++i)
        if (i < table.ColumnStyles.Count && table.ColumnStyles[i].SizeType == SizeType.Absolute)
          widths[i] = table.ColumnStyles[i].Width;
      foreach (Control child in table.Controls)
      {
        if (!child.Visible) continue;
        var position = table.GetPositionFromControl(child);
        if (position.Column < 0 || position.Column >= widths.Length) continue;
        int span = Math.Min(table.GetColumnSpan(child), widths.Length - position.Column);
        int minimum = child.MinimumSize.Width;
        var nested = child as TableLayoutPanel;
        if (nested != null) minimum = Math.Max(minimum, MinimumTableWidth(nested));
        else if (child is TextBoxBase || child is ComboBox) minimum = Math.Max(minimum, Pixels(80, child.DeviceDpi));
        else if (child.AutoSize) minimum = Math.Max(minimum, child.GetPreferredSize(Size.Empty).Width);
        minimum += child.Margin.Horizontal;
        float available = 0;
        for (int i = position.Column; i < position.Column + span; ++i) available += widths[i];
        if (minimum > available)
          for (int i = position.Column; i < position.Column + span; ++i) widths[i] += (minimum - available) / span;
      }
      float percentTotal = 0, percentUnit = 0, fixedTotal = table.Padding.Horizontal;
      for (int i = 0; i < widths.Length; ++i)
      {
        var style = i < table.ColumnStyles.Count ? table.ColumnStyles[i] : null;
        if (style != null && style.SizeType == SizeType.Percent && style.Width > 0)
        {
          percentTotal += style.Width;
          percentUnit = Math.Max(percentUnit, widths[i] / style.Width);
        }
        else fixedTotal += widths[i];
      }
      return (int)Math.Ceiling(fixedTotal + percentTotal * percentUnit);
    }
    private static void Collect(Control control, List<Control> result)
    {
      result.Add(control);
      foreach (Control child in control.Controls) Collect(child, result);
    }
    private static int Scale(int value, float ratio) { return (int)Math.Round(value * ratio); }
    private static Padding Scale(Padding value, float ratio)
    {
      return new Padding(Scale(value.Left, ratio), Scale(value.Top, ratio), Scale(value.Right, ratio), Scale(value.Bottom, ratio));
    }
    private sealed class Binding
    {
      private readonly Control control;
      public bool Pending;
      public bool PreserveWindowBounds;
      public bool LayoutApplied { get { return Math.Abs(layoutScale - LayoutScale) < 0.001f; } }
      public SizeF DocumentSize;
      private bool captured;
      private float layoutScale = 1f;
      private Font originalFont, ownedFont;
      private int lastDpi;
      private Size tabSize;
      private Padding tabPadding;
      private int maxTabWidth, treeRow, treeIndent;
      private readonly Dictionary<string, int> layoutMetrics = new Dictionary<string, int>();
      private readonly ConditionalWeakTable<object, ColumnMetric> columnMetrics = new ConditionalWeakTable<object, ColumnMetric>();
      private sealed class ColumnMetric { public float Scale = 1f; }
      public Binding(Control control)
      {
        this.control = control;
        control.HandleCreated += (s, e) => Queue(control);
        control.ParentChanged += (s, e) => Queue(control);
        control.DpiChangedAfterParent += (s, e) => Queue(control);
        control.ControlAdded += (s, e) => { Attach(e.Control); Queue(control); };
        control.Disposed += (s, e) => { if (ownedFont != null) ownedFont.Dispose(); };
        control.FontChanged += (s, e) =>
        {
          if (!applying && captured && control.Font != ownedFont)
          {
            if (control.DeviceDpi == lastDpi && TypeDescriptor.GetProperties(control)["Font"].ShouldSerializeValue(control)) originalFont = control.Font;
            Queue(control);
          }
        };
        var form = control as Form;
        if (form != null) form.DpiChanged += (s, e) => Queue(control);
        var grid = control as DataGridView;
        if (grid != null) grid.ColumnAdded += (s, e) => Queue(control);
        var strip = control as ToolStrip;
        if (strip != null) strip.ItemAdded += (s, e) => Queue(control);
      }
      public void Capture()
      {
        if (captured) return;
        captured = true;
        originalFont = control is ToolStripDropDown ? SystemFonts.MenuFont : control.Font;
        lastDpi = control.DeviceDpi;
        if (!(control is ToolStrip) && !TypeDescriptor.GetProperties(control)["Font"].ShouldSerializeValue(control))
          for (var ancestor = control.Parent; ancestor != null; ancestor = ancestor.Parent)
            if (bindings.TryGetValue(ancestor, out var parent) && parent.captured) { originalFont = parent.originalFont; break; }
        for (var ancestor = control.Parent; ancestor != null; ancestor = ancestor.Parent)
          if (bindings.TryGetValue(ancestor, out var parent) && parent.ownedFont != null && control.Font.Equals(parent.ownedFont))
          { originalFont = parent.originalFont; break; }
        // mforms LayoutBox/Table expose these layout-engine metrics; they are
        // not covered by Control.Scale or WinForms' Margin/Padding.
        foreach (string name in new[] { "Spacing", "RowSpacing", "ColumnSpacing" })
        {
          var property = TypeDescriptor.GetProperties(control)[name];
          if (property != null && !property.IsReadOnly && property.PropertyType == typeof(int))
            layoutMetrics[name] = (int)property.GetValue(control);
        }
        var tabs = control as TabControl;
        if (tabs != null) tabSize = tabs.ItemSize;
        var flat = control as FlatTabControl;
        if (flat != null) { tabPadding = flat.ItemPadding; maxTabWidth = flat.MaxTabSize; }
        var tree = control as TreeViewAdv;
        if (tree != null) { treeRow = tree.RowHeight; treeIndent = tree.Indent; }
      }
      public void Apply()
      {
        float ratio = LayoutScale / layoutScale;
        var container = control as ContainerControl;
        // Explicit geometry zoom owns application scaling. Keep WinForms in charge
        // of monitor DPI, without a second Font autoscale when text gets larger.
        if (container != null && container.AutoScaleMode == AutoScaleMode.Font)
        {
          container.AutoScaleMode = AutoScaleMode.Dpi;
          container.AutoScaleDimensions = new SizeF(control.DeviceDpi, control.DeviceDpi);
        }
        if (Math.Abs(ratio - 1f) > 0.001f)
        {
          layoutScale = LayoutScale; // Set before layout callbacks/reparenting.
          var bounds = control.Bounds;
          var window = control as Form;
          if (window != null && window.TopLevel && !PreserveWindowBounds)
            window.ClientSize = new Size(Scale(window.ClientSize.Width, ratio), Scale(window.ClientSize.Height, ratio));
          if (!(control is TabPage) && !(control is Form && ((Form)control).TopLevel))
          {
            if (control.Dock == DockStyle.None)
              control.Bounds = new Rectangle(Scale(bounds.X, ratio), Scale(bounds.Y, ratio), Scale(bounds.Width, ratio), Scale(bounds.Height, ratio));
            else if (control.Dock == DockStyle.Top || control.Dock == DockStyle.Bottom) control.Height = Scale(bounds.Height, ratio);
            else if (control.Dock == DockStyle.Left || control.Dock == DockStyle.Right) control.Width = Scale(bounds.Width, ratio);
          }
          control.Margin = Scale(control.Margin, ratio);
          control.Padding = Scale(control.Padding, ratio);
          control.MinimumSize = new Size(Scale(control.MinimumSize.Width, ratio), Scale(control.MinimumSize.Height, ratio));
          control.MaximumSize = new Size(Scale(control.MaximumSize.Width, ratio), Scale(control.MaximumSize.Height, ratio));
          var table = control as TableLayoutPanel;
          if (table != null)
          {
            foreach (RowStyle row in table.RowStyles) if (row.SizeType == SizeType.Absolute) row.Height *= ratio;
            foreach (ColumnStyle col in table.ColumnStyles) if (col.SizeType == SizeType.Absolute) col.Width *= ratio;
          }
          var split = control as SplitContainer;
          if (split != null)
          {
            split.SplitterWidth = Math.Max(2, Scale(split.SplitterWidth, ratio));
            int length = split.Orientation == Orientation.Horizontal ? split.Height : split.Width;
            int distance = Math.Min(length - split.Panel2MinSize - split.SplitterWidth, Scale(split.SplitterDistance, ratio));
            if (distance >= split.Panel1MinSize) split.SplitterDistance = distance;
          }
        }
        FontRole role = Role();
        if (HasText())
        {
          float points = FontSize(role, originalFont.SizeInPoints);
          if (ownedFont == null || control.Font != ownedFont || Math.Abs(control.Font.SizeInPoints - points) > 0.05f || control.Font.Style != originalFont.Style)
          {
            var previous = ownedFont;
            ownedFont = new Font(originalFont.FontFamily, points, originalFont.Style, GraphicsUnit.Point);
            control.Font = ownedFont;
            if (previous != null) previous.Dispose();
          }
        }
        float metric = LayoutScale * control.DeviceDpi / 96f;
        if (!DocumentSize.IsEmpty)
          ((ScrollableControl)control).AutoScrollMinSize = new Size((int)Math.Ceiling(DocumentSize.Width * metric), (int)Math.Ceiling(DocumentSize.Height * metric));
        foreach (var entry in layoutMetrics)
          TypeDescriptor.GetProperties(control)[entry.Key].SetValue(control, Scale(entry.Value, metric));
        var tree = control as TreeViewAdv;
        if (tree != null)
        {
          tree.RowHeight = Math.Max(Pixels(treeRow, control.DeviceDpi), control.Font.Height + Pixels(4, control.DeviceDpi));
          tree.Indent = Scale(treeIndent, metric);
          tree.GlyphScale = metric;
          foreach (TreeColumn column in tree.Columns)
          {
            var previous = columnMetrics.GetValue(column, c => new ColumnMetric());
            column.Width = Math.Max(1, Scale(column.Width, metric / previous.Scale));
            previous.Scale = metric;
          }
        }
        var nativeTree = control as TreeView;
        if (nativeTree != null) { nativeTree.ItemHeight = control.Font.Height + Pixels(4, control.DeviceDpi); nativeTree.Indent = Pixels(16, control.DeviceDpi); }
        var tabs = control as TabControl;
        if (tabs != null)
        {
          tabs.ItemSize = new Size(Scale(tabSize.Width, metric), Math.Max(Pixels(tabSize.Height, control.DeviceDpi), control.Font.Height + Pixels(6, control.DeviceDpi)));
          var flat = tabs as FlatTabControl;
          if (flat != null) { flat.ItemPadding = Scale(tabPadding, metric); flat.MaxTabSize = Scale(maxTabWidth, metric); flat.GlyphScale = metric; }
        }
        var grid = control as DataGridView;
        if (grid != null)
        {
          grid.DefaultCellStyle.Font = control.Font;
          grid.ColumnHeadersDefaultCellStyle.Font = control.Font;
          grid.RowHeadersDefaultCellStyle.Font = control.Font;
          grid.RowTemplate.Height = control.Font.Height + Pixels(6, control.DeviceDpi);
          grid.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.DisplayedCellsExceptHeaders;
          grid.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
          foreach (DataGridViewColumn column in grid.Columns)
          {
            var previous = columnMetrics.GetValue(column, c => new ColumnMetric());
            column.Width = Math.Max(column.MinimumWidth, Scale(column.Width, metric / previous.Scale));
            previous.Scale = metric;
          }
          if (grid.EditingControl != null) grid.EditingControl.Font = control.Font;
        }
        var combo = control as ComboBox;
        if (combo != null) combo.ItemHeight = control.Font.Height + Pixels(6, control.DeviceDpi);
        if (control is TextBoxBase && !((TextBoxBase)control).Multiline || control is ComboBox || control is NumericUpDown || control is ButtonBase)
          control.MinimumSize = new Size(control.MinimumSize.Width, Math.Max(control.MinimumSize.Height, control.Font.Height + Pixels(6, control.DeviceDpi)));
        var button = control as ButtonBase;
        if ((button is CheckBox || button is RadioButton) && button.AutoSize && button.Image == null)
        {
          // Native preferred sizes reserve only the unzoomed checkbox glyph.
          int textWidth = TextRenderer.MeasureText(button.Text, button.Font, Size.Empty, TextFormatFlags.NoPadding | TextFormatFlags.SingleLine).Width;
          int glyphWidth = Pixels(13 + 4, control.DeviceDpi) + 2;
          control.MinimumSize = new Size(textWidth + glyphWidth + control.Padding.Horizontal, control.MinimumSize.Height);
        }
        var strip = control as ToolStrip;
        if (strip != null)
        {
          strip.AutoSize = true;
          strip.ImageScalingSize = new Size(Pixels(16, control.DeviceDpi), Pixels(16, control.DeviceDpi));
          foreach (ToolStripItem item in strip.Items) items.GetValue(item, i => new ItemBinding(i)).Apply(strip);
        }
        lastDpi = control.DeviceDpi;
        control.Invalidate();
      }
      private FontRole Role()
      {
        if (control is MenuStrip || control is ToolStripDropDown) return FontRole.Menu;
        if (control is TreeView || control is TreeViewAdv || control is ListView) return FontRole.Navigation;
        if (control is TabControl) return FontRole.Tab;
        if (control is DataGridView) return FontRole.Grid;
        if (control is StatusStrip) return FontRole.Secondary;
        if (control is GroupBox || control is HeaderPanel) return FontRole.Heading;
        if (control is TextBoxBase || control is ComboBox || control is ButtonBase || control is NumericUpDown) return FontRole.Input;
        return FontRole.General;
      }
      private bool HasText()
      {
        return control is Label || control is ButtonBase || control is TextBoxBase || control is ComboBox ||
          control is UpDownBase || control is TreeView || control is TreeViewAdv || control is ListView ||
          control is DataGridView || control is PropertyGrid || control is ToolStrip || control is TabControl ||
          control is GroupBox || control is HeaderPanel;
      }
    }
    private sealed class ItemBinding
    {
      private readonly ToolStripItem item;
      private readonly string family;
      private readonly FontStyle style;
      private Font owned;
      public ItemBinding(ToolStripItem item)
      {
        this.item = item;
        // Unset item fonts inherit an already enlarged strip; use the original
        // system menu baseline instead of multiplying that inherited value again.
        family = item.Font.FontFamily.Name;
        style = item.Font.Style;
        var drop = item as ToolStripDropDownItem;
        if (drop != null) drop.DropDownOpening += (s, e) => { Attach(drop.DropDown); UiScale.Apply(drop.DropDown); };
        item.Disposed += (s, e) => { if (owned != null) owned.Dispose(); };
      }
      public void Apply(ToolStrip owner)
      {
        if (owner == null) return;
        bool menu = owner is MenuStrip || owner is ToolStripDropDown;
        float size = FontSize(menu ? FontRole.Menu : FontRole.General, SystemFonts.MenuFont.SizeInPoints);
        if (Math.Abs(item.Font.SizeInPoints - size) > 0.05f)
        {
          var previous = owned;
          owned = new Font(family, size, style, GraphicsUnit.Point);
          item.Font = owned;
          if (previous != null) previous.Dispose();
        }
        item.Padding = new Padding(Pixels(4, owner.DeviceDpi), Pixels(2, owner.DeviceDpi), Pixels(4, owner.DeviceDpi), Pixels(2, owner.DeviceDpi));
        if (!(item is ToolStripSeparator)) item.AutoSize = true;
        var host = item as ToolStripControlHost;
        if (host != null) Attach(host.Control);
      }
    }
  }
}
