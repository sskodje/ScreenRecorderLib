using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;

namespace TestApp
{
    public class OverlayTemplateSelector : System.Windows.Controls.DataTemplateSelector
    {
        public override DataTemplate SelectTemplate(object item, DependencyObject container)
        {
            OverlayModel model = (OverlayModel)item;
            if (model.Overlay is CameraCaptureOverlay)
            {
                return (container as FrameworkElement).FindResource("CameraCaptureOverlayTemplate") as DataTemplate;
            }
            else if(model.Overlay is VideoOverlay)
            {
                return (container as FrameworkElement).FindResource("VideoOverlayTemplate") as DataTemplate;
            }
            else if (model.Overlay is PictureOverlay)
            {
                return (container as FrameworkElement).FindResource("PictureOverlayTemplate") as DataTemplate;
            }
            return null;
        }
    }
}
