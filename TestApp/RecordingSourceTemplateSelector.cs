using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using TestApp.Sources;

namespace TestApp
{
    public class RecordingSourceTemplateSelector : System.Windows.Controls.DataTemplateSelector
    {
        public override DataTemplate SelectTemplate(object item, DependencyObject container)
        {

            if (item.GetType().Equals(typeof(CheckableRecordableWindow)))
                return (container as FrameworkElement).FindResource("WindowRecordingSourceTemplate") as DataTemplate;
            else if (item.GetType().Equals(typeof(CheckableRecordableDisplay)))
                return (container as FrameworkElement).FindResource("DisplayRecordingSourceTemplate") as DataTemplate;
            else if (item.GetType().Equals(typeof(CheckableRecordableCamera)))
                return (container as FrameworkElement).FindResource("CameraRecordingSourceTemplate") as DataTemplate;
            else if (item.GetType().Equals(typeof(CheckableRecordableVideo)))
                return (container as FrameworkElement).FindResource("VideoRecordingSourceTemplate") as DataTemplate;
            else if (item.GetType().Equals(typeof(CheckableRecordableImage)))
                return (container as FrameworkElement).FindResource("ImageRecordingSourceTemplate") as DataTemplate;
            return null;
        }
    }
}
