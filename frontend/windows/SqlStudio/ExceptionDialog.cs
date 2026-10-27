/*
 * Copyright (c) 2026 dev4fun. All rights reserved.
 * Copyright (c) 2009, 2019, Oracle and/or its affiliates. All rights reserved.

 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA 
 */

using System;
using System.Windows.Forms;
using MySQL.Controls;
using MySQL.Utilities.SysUtils;
using MySQL.SqlStudio;

namespace MySQL.GUI.SqlStudio
{
  public partial class ExceptionDialog : Form
  {
    private static ExceptionDialog singleton = new ExceptionDialog();

    private String errorInfo;

    private WbContext wbContext;

    protected ExceptionDialog()
    {
      InitializeComponent();
      MinimumSize = Size;
      ControlTheme.AttachSurface(this);
    }

    protected override void OnLayout(LayoutEventArgs e)
    {
      base.OnLayout(e);
      if (closeButton == null || reportBugButton == null || copyInfoButton == null || richTextBox1 == null)
        return;

      // Measure the current font after autoscaling; fixed designer heights can
      // clip the text when the flat dark-theme buttons are rendered at high DPI.
      int gap = Math.Max(6, Font.Height / 2);
      int margin = gap * 2;
      Button[] buttons = { closeButton, reportBugButton, copyInfoButton };
      int height = Font.Height + gap * 2;
      foreach (Button button in buttons)
        height = Math.Max(height, button.GetPreferredSize(System.Drawing.Size.Empty).Height);

      int right = ClientSize.Width - margin;
      int top = ClientSize.Height - margin - height;
      foreach (Button button in buttons)
      {
        if (button == copyInfoButton && !button.Visible)
          continue;
        int width = Math.Max(button.Width, button.GetPreferredSize(System.Drawing.Size.Empty).Width + gap * 2);
        button.SetBounds(right - width, top, width, height);
        right -= width + gap;
      }
      richTextBox1.Height = Math.Max(0, top - gap - richTextBox1.Top);
    }

    protected override void OnHandleCreated(EventArgs e)
    {
      base.OnHandleCreated(e);
      ApplyDialogTheme();
    }

    protected override void OnShown(EventArgs e)
    {
      ApplyDialogTheme();
      base.OnShown(e);
    }

    private void ApplyDialogTheme()
    {
      bool dark = Conversions.InDarkMode() && !SystemInformation.HighContrast;
      var background = dark
        ? Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, false)
        : System.Drawing.SystemColors.Control;
      var foreground = dark
        ? Conversions.GetApplicationColor(ApplicationColor.AppColorPanelContentArea, true)
        : System.Drawing.SystemColors.ControlText;
      richTextBox1.BackColor = background;
      richTextBox1.ForeColor = foreground;
      contextMenuStrip1.BackColor = background;
      contextMenuStrip1.ForeColor = foreground;
      contextMenuStrip1.Renderer = dark
        ? (ToolStripRenderer)new FlatSubToolStripRenderer()
        : new ToolStripProfessionalRenderer();

      if (!IsHandleCreated)
        return;

      int enabled = dark ? 1 : 0;
      try
      {
        int result = Win32.DwmSetWindowAttribute(Handle, Win32.DWMWA_USE_IMMERSIVE_DARK_MODE,
          ref enabled, sizeof(int));
        if (result != 0)
          Win32.DwmSetWindowAttribute(Handle, Win32.DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1,
            ref enabled, sizeof(int));
      }
      catch (DllNotFoundException) { }
      catch (EntryPointNotFoundException) { }
    }

    public static void Show(String message, String info, WbContext wbcontext)
    {
      singleton.messageLabel.Text = message;
      singleton.errorInfo = info;
      singleton.wbContext = wbcontext;
      singleton.ShowDialog();
    }

    private void reportBugButton_Click(object sender, EventArgs e)
    {
      if (wbContext.is_commercial())
        System.Diagnostics.Process.Start("http://support.oracle.com");
      else
        //wbContext.report_bug(errorInfo);
        System.Diagnostics.Process.Start("http://bugs.mysql.com");
    }

    private void copyInfoButton_Click(object sender, EventArgs e)
    {
      Clipboard.SetText(errorInfo);
    }

    private void copyStackTraceToClipboardToolStripMenuItem_Click(object sender, EventArgs e)
    {
      Clipboard.SetText(errorInfo);
    }

  }
}
