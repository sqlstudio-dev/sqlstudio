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
      var binding = bindings.GetValue(control, item => new Binding(item));
      // Only standard layout controls participate automatically. Custom-painted
      // panels (including diagram canvases and headers) own their palettes.
      Type type = control.GetType();
      if (type == typeof(Panel) || type == typeof(FlowLayoutPanel) ||
          type == typeof(TableLayoutPanel) || type == typeof(SplitContainer) ||
          type == typeof(SplitterPanel) || type == typeof(TabPage) ||
          type == typeof(Label) || type == typeof(Button) ||
          type == typeof(CheckBox) || type == typeof(RadioButton) || type == typeof(GroupBox))
        binding.EnableSurface();
      foreach (Control child in control.Controls)
        Attach(child);
    }

    // Custom layout surfaces can opt in explicitly; canvases keep their own palette.
    public static void AttachSurface(Control control)
    {
      Attach(control);
      bindings.GetValue(control, item => new Binding(item)).EnableSurface();
    }

    // Keep native editing, dropdown behaviour and accessibility; only repaint chrome.
    private sealed class InputFrame : NativeWindow
    {
      private readonly Control control;
      [System.Runtime.InteropServices.DllImport("user32.dll")]
      private static extern IntPtr GetWindowDC(IntPtr hwnd);
      [System.Runtime.InteropServices.DllImport("user32.dll")]
      private static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);

      public InputFrame(Control control) { this.control = control; }
      public void Attach()
      {
        if (Handle != control.Handle)
        {
          Detach();
          AssignHandle(control.Handle);
        }
      }
      public void Detach() { if (Handle != IntPtr.Zero) ReleaseHandle(); }

      protected override void WndProc(ref Message m)
      {
        base.WndProc(ref m);
        if ((m.Msg != 0x000F && m.Msg != 0x0085) || control.IsDisposed ||
            !Conversions.InDarkMode() || SystemInformation.HighContrast)
          return;

        var combo = control as ComboBox;
        // Simple combos have a permanent list instead of a dropdown button.
        if (combo != null && combo.DropDownStyle == ComboBoxStyle.Simple) return;
        var text = control as TextBoxBase;
        if (text != null && text.BorderStyle == BorderStyle.None) return;
        IntPtr dc = GetWindowDC(Handle);
        if (dc == IntPtr.Zero) return;
        try
        {
          using (Graphics graphics = Graphics.FromHdc(dc))
          using (Pen border = new Pen(Color.FromArgb(90, 90, 90)))
          {
            if (combo != null)
            {
              int width = SystemInformation.VerticalScrollBarWidth;
              int x = combo.RightToLeft == RightToLeft.Yes ? 1 : control.Width - width - 1;
              Rectangle button = new Rectangle(x, 1, width, control.Height - 2);
              using (Brush fill = new SolidBrush(Color.FromArgb(60, 60, 60)))
                graphics.FillRectangle(fill, button);
              int cx = button.Left + button.Width / 2;
              int cy = button.Top + button.Height / 2;
              using (Pen arrow = new Pen(control.Enabled ? Color.FromArgb(204, 204, 204) : Color.Gray))
              {
                graphics.DrawLine(arrow, cx - 4, cy - 2, cx, cy + 2);
                graphics.DrawLine(arrow, cx, cy + 2, cx + 4, cy - 2);
              }
            }
            graphics.DrawRectangle(border, 0, 0, control.Width - 1, control.Height - 1);
            // Cover the second pixel of the native sunken text-field border.
            if (text != null && text.BorderStyle == BorderStyle.Fixed3D)
              graphics.DrawRectangle(border, 1, 1, control.Width - 3, control.Height - 3);
          }
        }
        finally { ReleaseDC(Handle, dc); }
      }
    }

    // Native scrollbar geometry keeps the painted thumb aligned with its hit target.
    private sealed class ScrollFrame : NativeWindow
    {
      [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
      private struct ScrollInfo
      {
        public int Size;
        public int Left, Top, Right, Bottom;
        public int ButtonSize, ThumbTop, ThumbBottom, Reserved;
        [System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.ByValArray, SizeConst = 6)]
        public int[] State;
      }
      [System.Runtime.InteropServices.DllImport("user32.dll")]
      private static extern bool GetScrollBarInfo(IntPtr hwnd, int objectId, ref ScrollInfo info);
      private readonly ScrollBar bar;
      public ScrollFrame(ScrollBar bar) { this.bar = bar; }
      public void Attach() { if (Handle != bar.Handle) { Detach(); AssignHandle(bar.Handle); } }
      public void Detach() { if (Handle != IntPtr.Zero) ReleaseHandle(); }
      protected override void WndProc(ref Message m)
      {
        base.WndProc(ref m);
        if (bar.IsDisposed || Handle == IntPtr.Zero || !Conversions.InDarkMode() || SystemInformation.HighContrast)
          return;
        // Native scrollbars can also redraw directly when their range or position changes.
        if (m.Msg != 0x000F && m.Msg != 0x00E0 && m.Msg != 0x00E2 && m.Msg != 0x00E6 &&
            m.Msg != 0x0200 && m.Msg != 0x0202 && m.Msg != 0x0113 && m.Msg != 0x02A3)
          return;
        var info = new ScrollInfo();
        info.Size = System.Runtime.InteropServices.Marshal.SizeOf(typeof(ScrollInfo));
        info.State = new int[6];
        if (!GetScrollBarInfo(Handle, -4, ref info)) return; // OBJID_CLIENT
        bool vertical = bar is VScrollBar;
        int length = vertical ? bar.Height : bar.Width;
        int breadth = vertical ? bar.Width : bar.Height;
        using (Graphics g = Graphics.FromHwnd(Handle))
        using (Brush track = new SolidBrush(Color.FromArgb(37, 37, 38)))
        using (Brush thumb = new SolidBrush(Color.FromArgb(104, 104, 104)))
        {
          g.FillRectangle(track, bar.ClientRectangle);
          if (bar.Enabled && info.ThumbBottom > info.ThumbTop)
          {
            Rectangle rect = vertical
              ? new Rectangle(3, info.ThumbTop, Math.Max(1, breadth - 6), info.ThumbBottom - info.ThumbTop)
              : new Rectangle(info.ThumbTop, 3, info.ThumbBottom - info.ThumbTop, Math.Max(1, breadth - 6));
            g.FillRectangle(thumb, rect);
          }
          for (int end = 0; end < 2; ++end)
          {
            int center = end == 0 ? info.ButtonSize / 2 : length - info.ButtonSize / 2;
            bool enabled = bar.Enabled && (info.State[end == 0 ? 1 : 5] & 1) == 0;
            using (Pen pen = new Pen(enabled ? Color.FromArgb(204, 204, 204) : Color.FromArgb(104, 104, 104)))
            {
              int direction = end == 0 ? -1 : 1;
              int middle = breadth / 2;
              if (vertical)
              {
                g.DrawLine(pen, middle - 3, center - direction * 2, middle, center + direction);
                g.DrawLine(pen, middle, center + direction, middle + 3, center - direction * 2);
              }
              else
              {
                g.DrawLine(pen, center - direction * 2, middle - 3, center + direction, middle);
                g.DrawLine(pen, center + direction, middle, center - direction * 2, middle + 3);
              }
            }
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
      private InputFrame inputFrame;
      private ScrollFrame scrollFrame;
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
        if (!themedText)
        {
          control.HandleCreated += HandleCreated;
          control.HandleDestroyed += HandleDestroyed;
        }
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
          control is TreeView || control is TreeViewAdv || control is NumericUpDown || control is ScrollBar;
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
        if (inputFrame == null && (control is ComboBox || control is TextBoxBase || control is NumericUpDown))
          inputFrame = new InputFrame(control);
        if (inputFrame != null) inputFrame.Attach();
        if (scrollFrame == null && control is ScrollBar) scrollFrame = new ScrollFrame((ScrollBar)control);
        if (scrollFrame != null) scrollFrame.Attach();
        UpdateColors();
        if (!subscribed)
        {
          ManagedNotificationCenter.AddObserver(this, "GNColorsChanged");
          subscribed = true;
        }
      }
      private void HandleDestroyed(object sender, EventArgs e)
      {
        if (inputFrame != null) inputFrame.Detach();
        if (scrollFrame != null) scrollFrame.Detach();
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
              ? (separator ? Color.FromArgb(69, 69, 69)
                : originalBackColor.IsEmpty || neutralBack ? background : originalBackColor)
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
            ? (input ? Color.FromArgb(60, 60, 60)
              : Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, false))
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
              tree.GridColor = Color.FromArgb(69, 69, 69);
              tree.LineColor = Color.FromArgb(89, 89, 89);
              tree.DragDropMarkColor = Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, true);
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
