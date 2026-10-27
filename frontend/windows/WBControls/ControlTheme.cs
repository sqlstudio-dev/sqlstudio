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

    private sealed class Binding : ISqlStudioObserver
    {
      private readonly Control control;
      private readonly bool field;
      private readonly bool themedText;
      private FlatStyle comboStyle;
      private bool initialized;
      private bool subscribed;
      private bool surface;
      private bool applying;
      private Color originalBackColor;
      private Color originalForeColor;
      private Color originalTreeGridColor;
      private Color originalTreeLineColor;
      private Color originalTreeDragDropMarkColor;
      private FlatStyle buttonStyle;
      private bool buttonVisualStyle;

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
          control is TreeView || control is TreeViewAdv || control is NumericUpDown;
        themedText = control is Label || control is CheckBox || control is RadioButton || control is GroupBox;
        var tree = control as TreeViewAdv;
        if (tree != null)
        {
          originalTreeGridColor = tree.GridColor;
          originalTreeLineColor = tree.LineColor;
          originalTreeDragDropMarkColor = tree.DragDropMarkColor;
        }
        control.ControlAdded += ChildAdded;
        if (field || themedText)
        {
          control.HandleCreated += HandleCreated;
          control.HandleDestroyed += HandleDestroyed;
          if (control.IsHandleCreated) HandleCreated(control, EventArgs.Empty);
        }
      }

      private void ChildAdded(object sender, ControlEventArgs e) { Attach(e.Control); }
      private void HandleCreated(object sender, EventArgs e)
      {
        if (!initialized)
        {
          var combo = control as ComboBox;
          if (combo != null) comboStyle = combo.FlatStyle;
          originalBackColor = LocalColor(control, "BackColor");
          originalForeColor = LocalColor(control, "ForeColor");
          var button = control as ButtonBase;
          if (button != null)
          {
            buttonStyle = button.FlatStyle;
            buttonVisualStyle = button.UseVisualStyleBackColor;
          }
          initialized = true;
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
        if (subscribed)
        {
          ManagedNotificationCenter.RemoveObserver(this, "GNColorsChanged");
          subscribed = false;
        }
      }
      public void HandleNotification(string name, IntPtr sender, Dictionary<string, string> info)
      {
        if (name == "GNColorsChanged" && !control.IsDisposed && !control.Disposing)
          UpdateColors();
      }
      private void UpdateColors()
      {
        if (applying) return;
        applying = true;
        try
        {
          bool dark = Conversions.InDarkMode() && !SystemInformation.HighContrast;
          if (surface)
          {
            Color background = Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, false);
            Color foreground = Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, true);
            bool transparent = !originalBackColor.IsEmpty && originalBackColor.A == 0;
            // Neutral colors are presentation, not semantic status colors. A page may
            // have been created with the dark palette, so never restore that as its light color.
            bool neutralBack = !originalBackColor.IsEmpty && originalBackColor.GetSaturation() < 0.1f;
            float brightness = originalBackColor.GetBrightness();
            bool separator = neutralBack && brightness > 0.25f && brightness < 0.8f &&
              (control.Height <= 2 || control.Width <= 2);
            control.BackColor = transparent ? originalBackColor : dark
              ? (separator ? SystemColors.ControlDark
                : originalBackColor.IsEmpty || neutralBack ? SystemColors.Control : originalBackColor)
              : (neutralBack && brightness < 0.25f ? SystemColors.Window : originalBackColor);
            bool neutralText = originalForeColor.IsEmpty || originalForeColor.GetSaturation() < 0.1f;
            control.ForeColor = neutralText ? (dark ? foreground : SystemColors.ControlText) : originalForeColor;
            if (SystemInformation.HighContrast)
            {
              control.BackColor = SystemColors.Control;
              control.ForeColor = SystemColors.ControlText;
            }
            var button = control as ButtonBase;
            if (button != null)
            {
              button.FlatStyle = dark ? FlatStyle.Flat : buttonStyle;
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
          bool input = control is TextBoxBase || control is ComboBox || control is NumericUpDown;
          control.BackColor = dark
            ? (input ? SystemColors.Window
              : SystemColors.Control)
            : SystemColors.Window;
          control.ForeColor = dark ? Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, true) : SystemColors.WindowText;
          var combo = control as ComboBox;
          if (combo != null) combo.FlatStyle = dark ? FlatStyle.Flat : comboStyle;
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
              tree.GridColor = SystemColors.ControlDark;
              tree.LineColor = SystemColors.GrayText;
              tree.DragDropMarkColor = SystemColors.Highlight;
            }
            else
            {
              tree.GridColor = originalTreeGridColor;
              tree.LineColor = originalTreeLineColor;
              tree.DragDropMarkColor = originalTreeDragDropMarkColor;
            }
            tree.HeaderBackColor = dark && !SystemInformation.HighContrast
              ? Conversions.GetApplicationColor(ApplicationColor.AppColorPanelToolbar, false) : Color.Empty;
            tree.HeaderForeColor = dark && !SystemInformation.HighContrast ? control.ForeColor : Color.Empty;
          }
          control.Invalidate();
        }
        finally { applying = false; }
      }
    }
  }
}
