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
            if (model.Overlay is VideoCaptureOverlay)
            {
                return (container as FrameworkElement).FindResource("CameraCaptureOverlayTemplate") as DataTemplate;
            }
            else if(model.Overlay is VideoOverlay)
            {
                return (container as FrameworkElement).FindResource("VideoOverlayTemplate") as DataTemplate;
            }
            else if (model.Overlay is ImageOverlay)
            {
                return (container as FrameworkElement).FindResource("PictureOverlayTemplate") as DataTemplate;
            }
            else if (model.Overlay is DisplayOverlay)
            {
                return (container as FrameworkElement).FindResource("DisplayOverlayTemplate") as DataTemplate;
            }
            else if (model.Overlay is WindowOverlay)
            {
                return (container as FrameworkElement).FindResource("WindowOverlayTemplate") as DataTemplate;
            }
            return null;
        }
    }
}
