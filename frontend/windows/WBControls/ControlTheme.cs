// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Runtime.CompilerServices;
using System.Windows.Forms;
using Aga.Controls.Tree;
using MySQL.SqlStudio;

namespace MySQL.Controls
{
  // Attach once per control, including controls inserted after a document is shown.
  // Each subscription follows its HWND lifetime, so closed documents are not retained.
  public static class ControlTheme
  {
    private static readonly ConditionalWeakTable<Control, Binding> bindings = new ConditionalWeakTable<Control, Binding>();

    public static void Attach(Control control)
    {
      if (control == null || control.IsDisposed) return;
      bindings.GetValue(control, item => new Binding(item));
      foreach (Control child in control.Controls)
        Attach(child);
    }

    // mforms opts layout surfaces in explicitly; custom canvases keep their own palette.
    public static void AttachSurface(Control control)
    {
      Attach(control);
      bindings.GetValue(control, item => new Binding(item)).EnableSurface();
    }

    private static readonly ToolStripRenderer DarkRenderer = new ThemeRenderer();

    private sealed class ThemeColorTable : ProfessionalColorTable
    {
      public ThemeColorTable() { UseSystemColors = false; }
      public override Color ToolStripGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color ToolStripGradientMiddle { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color ToolStripGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color MenuStripGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color MenuStripGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color ToolStripDropDownBackground { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color ImageMarginGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color ImageMarginGradientMiddle { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color ImageMarginGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false); } }
      public override Color MenuBorder { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorBorder, false); } }
      public override Color ToolStripBorder { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorBorder, false); } }
      public override Color SeparatorDark { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorBorder, false); } }
      public override Color SeparatorLight { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorBorder, false); } }
      public override Color ButtonSelectedBorder { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorAccent, false); } }
      public override Color ButtonPressedBorder { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorAccent, false); } }
      public override Color ButtonSelectedGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color ButtonSelectedGradientMiddle { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color ButtonSelectedGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color ButtonPressedGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color ButtonPressedGradientMiddle { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color ButtonPressedGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color ButtonCheckedGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorSelection, false); } }
      public override Color ButtonCheckedGradientMiddle { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorSelection, false); } }
      public override Color ButtonCheckedGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorSelection, false); } }
      public override Color MenuItemSelected { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color MenuItemSelectedGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color MenuItemSelectedGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color MenuItemPressedGradientBegin { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color MenuItemPressedGradientMiddle { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color MenuItemPressedGradientEnd { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color MenuItemBorder { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorAccent, false); } }
      public override Color CheckBackground { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorSelection, false); } }
      public override Color CheckSelectedBackground { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorHover, false); } }
      public override Color CheckPressedBackground { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorPressed, false); } }
      public override Color GripDark { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorBorder, false); } }
      public override Color GripLight { get { return Conversions.GetApplicationColor(ApplicationColor.AppColorBorder, false); } }
    }

    private sealed class ThemeRenderer : ToolStripProfessionalRenderer
    {
      public ThemeRenderer() : base(new ThemeColorTable()) { RoundedEdges = false; }
      protected override void OnRenderItemText(ToolStripItemTextRenderEventArgs e)
      {
        e.TextColor = Conversions.GetApplicationColor(e.Item.Enabled ? ApplicationColor.AppColorControl : ApplicationColor.AppColorDisabled, true);
        base.OnRenderItemText(e);
      }
      protected override void OnRenderArrow(ToolStripArrowRenderEventArgs e)
      {
        e.ArrowColor = Conversions.GetApplicationColor(e.Item.Enabled ? ApplicationColor.AppColorControl : ApplicationColor.AppColorDisabled, true);
        base.OnRenderArrow(e);
      }
    }

    // ComboBox does not expose Paint and its native flat arrow remains light.
    // Overlay only its chrome; editing, dropdown, accessibility and keyboard handling stay native.
    private sealed class ComboChrome : NativeWindow
    {
      private readonly ComboBox combo;
      public ComboChrome(ComboBox combo) { this.combo = combo; }
      protected override void WndProc(ref Message m)
      {
        base.WndProc(ref m);
        if ((m.Msg != 0x000f && m.Msg != 0x0317 && m.Msg != 0x0318) ||
            !Conversions.InDarkMode() || SystemInformation.HighContrast || combo.IsDisposed) return;
        using (var graphics = m.Msg == 0x000f ? Graphics.FromHwnd(Handle) : Graphics.FromHdc(m.WParam))
        {
          int width = Math.Min(combo.Width, (int)Math.Ceiling(17 * graphics.DpiX / 96f));
          var button = new Rectangle(combo.RightToLeft == RightToLeft.Yes ? 0 : combo.Width - width, 0, width, combo.Height);
          var role = !combo.Enabled ? ApplicationColor.AppColorDisabled : combo.DroppedDown ? ApplicationColor.AppColorPressed : ApplicationColor.AppColorControl;
          using (var brush = new SolidBrush(Conversions.GetApplicationColor(role, false))) graphics.FillRectangle(brush, button);
          int x = button.Left + button.Width / 2, y = button.Top + button.Height / 2;
          int size = Math.Max(2, (int)Math.Round(3 * graphics.DpiX / 96f));
          using (var brush = new SolidBrush(Conversions.GetApplicationColor(role, true)))
            graphics.FillPolygon(brush, new[] { new Point(x - size, y - 1), new Point(x + size, y - 1), new Point(x, y + size - 1) });
          using (var pen = new Pen(Conversions.GetApplicationColor(combo.Focused ? ApplicationColor.AppColorAccent : ApplicationColor.AppColorBorder, false)))
          {
            graphics.DrawRectangle(pen, 0, 0, Math.Max(0, combo.Width - 1), Math.Max(0, combo.Height - 1));
            graphics.DrawRectangle(pen, combo.RightToLeft == RightToLeft.Yes ? width : 1, 1,
              Math.Max(0, combo.Width - width - 2), Math.Max(0, combo.Height - 3));
          }
        }
      }
    }

    private sealed class Binding : ISqlStudioObserver
    {
      private readonly Control control;
      private readonly bool field;
      private readonly bool themedText;
      private FlatStyle comboStyle;
      private DrawMode comboDrawMode;
      private BorderStyle textBorder;
      private bool initialized;
      private bool subscribed;
      private bool surface;
      private bool applying;
      private bool styleUpdatePending;
      private int handleGeneration;
      private ComboChrome comboChrome;
      private Color originalBackColor;
      private Color originalForeColor;
      private Color originalTreeGridColor;
      private Color originalTreeLineColor;
      private Color originalTreeDragDropMarkColor;
      private FlatStyle buttonStyle;
      private bool buttonVisualStyle;
      private Color buttonBorder, buttonHover, buttonPressed;
      private bool tabVisualStyle;
      private Color tabBackground;
      private ToolStripRenderer stripRenderer;
      private bool gridHeadersVisualStyle;

      private static Color LocalColor(Control control, string property)
      {
        var descriptor = TypeDescriptor.GetProperties(control)[property];
        return descriptor.ShouldSerializeValue(control) ? (Color)descriptor.GetValue(control) : Color.Empty;
      }

      public void EnableSurface()
      {
        if (surface || field) return;
        surface = true;
        control.HandleCreated += HandleCreated;
        control.HandleDestroyed += HandleDestroyed;
        control.BackColorChanged += SurfaceBackColorChanged;
        control.ForeColorChanged += SurfaceForeColorChanged;
        if (control.IsHandleCreated) HandleCreated(control, EventArgs.Empty);
      }

      private void SurfaceBackColorChanged(object sender, EventArgs e)
      {
        if (!initialized || applying) return;
        originalBackColor = LocalColor(control, "BackColor");
        UpdateColors();
      }
      private void SurfaceForeColorChanged(object sender, EventArgs e)
      {
        if (!initialized || applying) return;
        originalForeColor = LocalColor(control, "ForeColor");
        UpdateColors();
      }

      public Binding(Control control)
      {
        this.control = control;
        field = control is TextBoxBase || control is ComboBox || control is ListBox ||
          control is TreeView || control is TreeViewAdv || control is NumericUpDown ||
          control is DataGridView || control is ListView;
        themedText = control is Label || control is CheckBox || control is RadioButton || control is GroupBox;
        var tree = control as TreeViewAdv;
        if (tree != null)
        {
          originalTreeGridColor = tree.GridColor;
          originalTreeLineColor = tree.LineColor;
          originalTreeDragDropMarkColor = tree.DragDropMarkColor;
        }
        control.ControlAdded += ChildAdded;
        control.EnabledChanged += StateChanged;
        control.Paint += PaintDisabledText;
        var selector = control as ComboBox;
        if (selector != null) selector.DrawItem += DrawComboItem;
        if ((control is Panel && !(control is HeaderPanel)) || control is Form ||
            control is UserControl || control is ButtonBase || control is Label || control is GroupBox ||
            control is SplitContainer || control is Splitter || control is FlatTabControl || control is ToolStrip)
          EnableSurface();
        if (field || (themedText && !surface))
        {
          control.HandleCreated += HandleCreated;
          control.HandleDestroyed += HandleDestroyed;
          if (control.IsHandleCreated) HandleCreated(control, EventArgs.Empty);
        }
      }

      private void DrawComboItem(object sender, DrawItemEventArgs e)
      {
        if (comboDrawMode != DrawMode.Normal || !Conversions.InDarkMode() || SystemInformation.HighContrast) return;
        var combo = (ComboBox)sender;
        bool selected = (e.State & DrawItemState.Selected) != 0;
        var role = !combo.Enabled ? ApplicationColor.AppColorDisabled : selected ? ApplicationColor.AppColorSelection : ApplicationColor.AppColorPanelContentArea;
        using (var brush = new SolidBrush(Palette(role))) e.Graphics.FillRectangle(brush, e.Bounds);
        string text = e.Index >= 0 ? combo.GetItemText(combo.Items[e.Index]) : combo.Text;
        TextRenderer.DrawText(e.Graphics, text, combo.Font, e.Bounds, Palette(role, true),
          TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix |
          (combo.RightToLeft == RightToLeft.Yes ? TextFormatFlags.RightToLeft | TextFormatFlags.Right : TextFormatFlags.Left));
        if ((e.State & DrawItemState.Focus) != 0) e.DrawFocusRectangle();
      }

      private void PaintDisabledText(object sender, PaintEventArgs e)
      {
        if (control.Enabled || !Conversions.InDarkMode() || SystemInformation.HighContrast) return;
        var button = control as ButtonBase;
        if (button == null || button.Image != null) return;
        // WinForms uses embossed system colors for disabled labels. Paint the
        // disabled state with the shared palette while keeping native interaction.
        Rectangle bounds = control.ClientRectangle;
        using (var brush = new SolidBrush(control.BackColor)) e.Graphics.FillRectangle(brush, bounds);
        ContentAlignment alignment = button.TextAlign;
        ContentAlignment checkAlignment = ContentAlignment.MiddleLeft;
        var check = control as CheckBox;
        var radio = control as RadioButton;
        if (check != null) checkAlignment = check.CheckAlign;
        if (radio != null) checkAlignment = radio.CheckAlign;
        if (check != null || radio != null)
        {
          int glyphSize = (int)Math.Ceiling(13 * e.Graphics.DpiX / 96f);
          int glyphWidth = glyphSize + (int)Math.Ceiling(4 * e.Graphics.DpiX / 96f);
          bool right = checkAlignment == ContentAlignment.TopRight || checkAlignment == ContentAlignment.MiddleRight || checkAlignment == ContentAlignment.BottomRight;
          if (control.RightToLeft == RightToLeft.Yes) right = !right;
          var glyph = new Rectangle(right ? bounds.Right - glyphSize - 1 : bounds.Left, (bounds.Height - glyphSize) / 2, glyphSize, glyphSize);
          if (checkAlignment == ContentAlignment.TopLeft || checkAlignment == ContentAlignment.TopCenter || checkAlignment == ContentAlignment.TopRight) glyph.Y = bounds.Top;
          if (checkAlignment == ContentAlignment.BottomLeft || checkAlignment == ContentAlignment.BottomCenter || checkAlignment == ContentAlignment.BottomRight) glyph.Y = bounds.Bottom - glyphSize;
          using (var pen = new Pen(Palette(ApplicationColor.AppColorDisabled, true)))
          {
            if (radio != null) e.Graphics.DrawEllipse(pen, glyph);
            else e.Graphics.DrawRectangle(pen, glyph);
          }
          bool marked = check != null ? check.Checked : radio.Checked;
          if (marked)
          {
            glyph.Inflate(-3, -3);
            using (var brush = new SolidBrush(Palette(ApplicationColor.AppColorDisabled, true)))
            {
              if (radio != null) e.Graphics.FillEllipse(brush, glyph);
              else if (check.CheckState == CheckState.Indeterminate) e.Graphics.FillRectangle(brush, glyph);
              else using (var pen = new Pen(Palette(ApplicationColor.AppColorDisabled, true), Math.Max(1, e.Graphics.DpiX / 96f)))
                e.Graphics.DrawLines(pen, new[] { new Point(glyph.Left, glyph.Top + glyph.Height / 2), new Point(glyph.Left + glyph.Width / 3, glyph.Bottom), new Point(glyph.Right, glyph.Top) });
            }
          }
          if (!right) bounds.X += glyphWidth;
          bounds.Width = Math.Max(0, bounds.Width - glyphWidth);
        }
        else bounds.Inflate(-3, -3);
        using (var brush = new SolidBrush(control.BackColor)) e.Graphics.FillRectangle(brush, bounds);
        TextFormatFlags flags = TextFormatFlags.NoPadding | TextFormatFlags.WordBreak;
        if (button.AutoEllipsis) flags |= TextFormatFlags.EndEllipsis;
        if (!button.UseMnemonic) flags |= TextFormatFlags.NoPrefix;
        if (alignment == ContentAlignment.TopCenter || alignment == ContentAlignment.MiddleCenter || alignment == ContentAlignment.BottomCenter) flags |= TextFormatFlags.HorizontalCenter;
        if (alignment == ContentAlignment.TopRight || alignment == ContentAlignment.MiddleRight || alignment == ContentAlignment.BottomRight) flags |= TextFormatFlags.Right;
        if (alignment == ContentAlignment.MiddleLeft || alignment == ContentAlignment.MiddleCenter || alignment == ContentAlignment.MiddleRight) flags |= TextFormatFlags.VerticalCenter;
        if (alignment == ContentAlignment.BottomLeft || alignment == ContentAlignment.BottomCenter || alignment == ContentAlignment.BottomRight) flags |= TextFormatFlags.Bottom;
        TextRenderer.DrawText(e.Graphics, control.Text, control.Font, bounds, Palette(ApplicationColor.AppColorDisabled, true), flags);
      }

      private void StateChanged(object sender, EventArgs e) { if (initialized) UpdateColors(); }

      private void ChildAdded(object sender, ControlEventArgs e) { Attach(e.Control); }
      private void HandleCreated(object sender, EventArgs e)
      {
        if (!initialized)
        {
          var combo = control as ComboBox;
          if (combo != null) { comboStyle = combo.FlatStyle; comboDrawMode = combo.DrawMode; }
          var text = control as TextBoxBase;
          if (text != null) textBorder = text.BorderStyle;
          originalBackColor = LocalColor(control, "BackColor");
          originalForeColor = LocalColor(control, "ForeColor");
          var button = control as ButtonBase;
          if (button != null)
          {
            buttonStyle = button.FlatStyle;
            buttonVisualStyle = button.UseVisualStyleBackColor;
            buttonBorder = button.FlatAppearance.BorderColor;
            buttonHover = button.FlatAppearance.MouseOverBackColor;
            buttonPressed = button.FlatAppearance.MouseDownBackColor;
          }
          var page = control as TabPage;
          if (page != null) tabVisualStyle = page.UseVisualStyleBackColor;
          var tabs = control as FlatTabControl;
          if (tabs != null) tabBackground = tabs.BackgroundColor;
          var strip = control as ToolStrip;
          if (strip != null) stripRenderer = strip.Renderer;
          var grid = control as DataGridView;
          if (grid != null) gridHeadersVisualStyle = grid.EnableHeadersVisualStyles;
          initialized = true;
        }
        if (control is ComboBox)
        {
          if (comboChrome == null) comboChrome = new ComboChrome((ComboBox)control);
          comboChrome.AssignHandle(control.Handle);
        }
        UpdateColors();
        if (!subscribed)
        {
          ManagedNotificationCenter.AddObserver(this, "GNColorsChanged");
          subscribed = true;
        }
      }
      private void HandleDestroyed(object sender, EventArgs e)
      {
        ++handleGeneration;
        styleUpdatePending = false;
        if (comboChrome != null) comboChrome.ReleaseHandle();
        if (subscribed)
        {
          ManagedNotificationCenter.RemoveObserver(this, "GNColorsChanged");
          subscribed = false;
        }
      }
      public void HandleNotification(string name, IntPtr sender, Dictionary<string, string> info)
      {
        if (name == "GNColorsChanged" && !control.IsDisposed && !control.Disposing)
        {
          if (control.InvokeRequired) control.BeginInvoke(new MethodInvoker(UpdateColors));
          else UpdateColors();
        }
      }
      private void QueueStyleUpdate()
      {
        if (!(control is ButtonBase || control is ComboBox || control is TextBoxBase) ||
            styleUpdatePending || !control.IsHandleCreated || control.IsDisposed || control.Disposing) return;
        styleUpdatePending = true;
        int generation = handleGeneration;
        // These properties recreate HWNDs. Defer both initial setup and runtime
        // changes: HandleCreated is inside CreateWindowEx, and GNColorsChanged
        // dispatch holds native observer pointers that HandleDestroyed unregisters.
        control.BeginInvoke(new MethodInvoker(delegate
        {
          if (generation != handleGeneration) return;
          styleUpdatePending = false;
          if (!control.IsHandleCreated || control.IsDisposed || control.Disposing) return;
          bool dark = Conversions.InDarkMode() && !SystemInformation.HighContrast;
          var button = control as ButtonBase;
          if (button != null) button.FlatStyle = dark ? FlatStyle.Flat : buttonStyle;
          var combo = control as ComboBox;
          if (combo != null)
          {
            combo.FlatStyle = dark ? FlatStyle.Flat : comboStyle;
            if (comboDrawMode == DrawMode.Normal) combo.DrawMode = dark ? DrawMode.OwnerDrawFixed : comboDrawMode;
          }
          var text = control as TextBoxBase;
          if (text != null && textBorder != BorderStyle.None)
            text.BorderStyle = dark ? BorderStyle.FixedSingle : textBorder;
        }));
      }

      private static Color Palette(ApplicationColor role, bool foreground = false)
      {
        return Conversions.GetApplicationColor(role, foreground);
      }

      private void UpdateColors()
      {
        if (applying || control.IsDisposed || control.Disposing) return;
        applying = true;
        try
        {
          bool dark = Conversions.InDarkMode() && !SystemInformation.HighContrast;
          if (surface)
          {
            Color background = Palette(ApplicationColor.AppColorPanelToolbar);
            Color foreground = Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, true);
            bool transparent = !originalBackColor.IsEmpty && originalBackColor.A == 0;
            // Neutral colors are presentation, not semantic status colors. A page may
            // have been created with the dark palette, so never restore that as its light color.
            bool neutralBack = !originalBackColor.IsEmpty && originalBackColor.GetSaturation() < 0.1f;
            float brightness = originalBackColor.GetBrightness();
            bool separator = neutralBack && brightness > 0.25f && brightness < 0.8f &&
              (control.Height <= 2 || control.Width <= 2);
            control.BackColor = transparent ? originalBackColor : dark
              ? (separator ? Palette(ApplicationColor.AppColorBorder)
                : originalBackColor.IsEmpty || neutralBack ? background : originalBackColor)
              : (neutralBack && brightness < 0.25f ? SystemColors.Window : originalBackColor);
            bool neutralText = originalForeColor.IsEmpty || originalForeColor.GetSaturation() < 0.1f;
            control.ForeColor = neutralText ? (dark ? foreground : SystemColors.ControlText) : originalForeColor;
            if (SystemInformation.HighContrast)
            {
              control.BackColor = SystemColors.Control;
              control.ForeColor = SystemColors.ControlText;
            }
            var page = control as TabPage;
            if (page != null) page.UseVisualStyleBackColor = !dark && tabVisualStyle;
            var split = control as SplitContainer;
            if (dark && (split != null || control is Splitter))
              control.BackColor = Palette(ApplicationColor.AppColorSplitter);
            var tabs = control as FlatTabControl;
            if (tabs != null)
            {
              tabs.BackgroundColor = dark ? background : tabBackground;
              tabs.UpdateColors();
            }
            var strip = control as ToolStrip;
            if (strip != null)
            {
              strip.Renderer = dark ? DarkRenderer : stripRenderer;
              strip.BackColor = dark ? background : originalBackColor;
            }
            var button = control as ButtonBase;
            if (button != null)
            {
              button.BackColor = dark ? Palette(control.Enabled ? ApplicationColor.AppColorControl : ApplicationColor.AppColorDisabled) : originalBackColor;
              button.ForeColor = dark ? Palette(control.Enabled ? ApplicationColor.AppColorControl : ApplicationColor.AppColorDisabled, true) : originalForeColor;
              button.FlatAppearance.BorderColor = dark ? Palette(ApplicationColor.AppColorBorder) : buttonBorder;
              button.FlatAppearance.MouseOverBackColor = dark ? Palette(ApplicationColor.AppColorHover) : buttonHover;
              button.FlatAppearance.MouseDownBackColor = dark ? Palette(ApplicationColor.AppColorPressed) : buttonPressed;
              button.UseVisualStyleBackColor = !dark && buttonVisualStyle;
            }
            control.Invalidate();
            return;
          }
          if (themedText)
          {
            bool neutralText = originalForeColor.IsEmpty || originalForeColor.GetSaturation() < 0.1f;
            if (SystemInformation.HighContrast)
              control.ForeColor = SystemColors.ControlText;
            else if (neutralText)
              control.ForeColor = dark
                ? Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, true)
                : (originalForeColor.IsEmpty ? SystemColors.ControlText : originalForeColor);
            control.Invalidate();
            return;
          }

          control.BackColor = dark
            ? Palette(control.Enabled ? ApplicationColor.AppColorPanelContentArea : ApplicationColor.AppColorDisabled)
            : SystemColors.Window;
          control.ForeColor = dark ? Palette(control.Enabled ? ApplicationColor.AppColorPanelContentArea : ApplicationColor.AppColorDisabled, true) : SystemColors.WindowText;
          var grid = control as DataGridView;
          if (grid != null)
          {
            grid.BackgroundColor = control.BackColor;
            grid.GridColor = dark ? Palette(ApplicationColor.AppColorBorder) : SystemColors.ControlDark;
            grid.EnableHeadersVisualStyles = !dark && gridHeadersVisualStyle;
            foreach (var style in new[] { grid.DefaultCellStyle, grid.RowsDefaultCellStyle, grid.AlternatingRowsDefaultCellStyle,
              grid.ColumnHeadersDefaultCellStyle, grid.RowHeadersDefaultCellStyle })
            {
              bool header = style == grid.ColumnHeadersDefaultCellStyle || style == grid.RowHeadersDefaultCellStyle;
              style.BackColor = header ? (dark ? Palette(ApplicationColor.AppColorPanelToolbar) : SystemColors.Control) : control.BackColor;
              style.ForeColor = control.ForeColor;
              style.SelectionBackColor = dark ? Palette(ApplicationColor.AppColorSelection) : SystemColors.Highlight;
              style.SelectionForeColor = dark ? Palette(ApplicationColor.AppColorSelection, true) : SystemColors.HighlightText;
            }
            if (grid.EditingControl != null)
            {
              grid.EditingControl.BackColor = control.BackColor;
              grid.EditingControl.ForeColor = control.ForeColor;
            }
          }
          var tree = control as TreeViewAdv;
          if (tree != null)
          {
            if (SystemInformation.HighContrast)
            {
              tree.GridColor = SystemColors.WindowText;
              tree.LineColor = SystemColors.WindowText;
              tree.DragDropMarkColor = SystemColors.Highlight;
            }
            else if (dark)
            {
              tree.GridColor = Palette(ApplicationColor.AppColorBorder);
              tree.LineColor = Palette(ApplicationColor.AppColorDisabled, true);
              tree.DragDropMarkColor = Palette(ApplicationColor.AppColorAccent);
            }
            else
            {
              tree.GridColor = originalTreeGridColor;
              tree.LineColor = originalTreeLineColor;
              tree.DragDropMarkColor = originalTreeDragDropMarkColor;
            }
            tree.SelectionBackColor = dark ? Palette(ApplicationColor.AppColorSelection) : Color.Empty;
            tree.SelectionForeColor = dark ? Palette(ApplicationColor.AppColorSelection, true) : Color.Empty;
            tree.InactiveSelectionBackColor = dark ? Palette(ApplicationColor.AppColorTopTabSelectedUnfocused) : Color.Empty;
            tree.HeaderBackColor = dark && !SystemInformation.HighContrast
              ? Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false) : Color.Empty;
            tree.HeaderForeColor = dark && !SystemInformation.HighContrast ? control.ForeColor : Color.Empty;
          }
          control.Invalidate();
        }
        finally
        {
          applying = false;
          QueueStyleUpdate();
        }
      }
    }
  }
}
