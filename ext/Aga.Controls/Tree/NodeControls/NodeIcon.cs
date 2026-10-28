/*
 * Copyright (c) 2026 dev4fun. All rights reserved.
 */

using System;
using System.Collections.Generic;
using System.Text;
using System.Drawing;
using System.Windows.Forms;
using Aga.Controls.Properties;

namespace Aga.Controls.Tree.NodeControls
{
	public class NodeIcon : BindableControl
	{
		public NodeIcon()
		{
			LeftMargin = 1;
		}

		public override Size MeasureSize(TreeNodeAdv node, DrawContext context)
		{
			Image image = GetIcon(node);
			if (image != null)
				return new Size((int)Math.Ceiling(image.Width * (Parent == null ? 1f : Parent.GlyphScale)),
          (int)Math.Ceiling(image.Height * (Parent == null ? 1f : Parent.GlyphScale)));
			else
				return Size.Empty;
		}

		public override void Draw(TreeNodeAdv node, DrawContext context)
		{
			Image image = GetIcon(node);
			if (image != null)
			{
				Rectangle r = GetBounds(node, context);
        // ml: center icon vertically.
        r.Y += (context.Bounds.Height - r.Height) / 2;
				context.Graphics.DrawImage(image, new Rectangle(r.Location, MeasureSize(node, context)));
			}
		}

		protected virtual Image GetIcon(TreeNodeAdv node)
		{
			return GetValue(node) as Image;
		}
	}
}
