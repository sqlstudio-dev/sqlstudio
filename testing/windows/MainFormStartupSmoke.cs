// Copyright (c) 2026 dev4fun. Licensed under GPL-2.0.
using System;
using System.Windows.Forms;
using MySQL.Controls;
using MySQL.GUI.SqlStudio;
using MySQL.SqlStudio;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.8")]

internal static class MainFormStartupSmoke
{
  [STAThread]
  private static int Main()
  {
    try
    {
      Application.SetUnhandledExceptionMode(UnhandledExceptionMode.ThrowException);
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);
      Logger.InitLogger(AppDomain.CurrentDomain.BaseDirectory);
      MySQL.Forms.Manager.get_instance();
      // Match Program.Main's ordering: callbacks need the form before init().
      // The native app_Application/options do not exist at this point.
      using (var context = new WbContext(false))
      using (var form = new MainForm(context))
      {
        if (form.IsDisposed || form.Controls.Count == 0)
          throw new Exception("The main form must be constructed before backend initialization.");
        if (Math.Abs(UiScale.Zoom - 1f) > 0.001f)
          throw new Exception("The pre-init form must retain the default zoom.");
      }
      Console.WriteLine("PASS: real MainForm construction and disposal before WbContext.init, without reading native application options.");
      return 0;
    }
    catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
  }
}
