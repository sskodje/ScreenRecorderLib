using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using TestApp.Sources;
using TestApp.Sources.Audio;

namespace TestApp
{
    public class AudioRecordingSourceTemplateSelector : System.Windows.Controls.DataTemplateSelector
    {
        public override DataTemplate SelectTemplate(object item, DependencyObject container)
        {

            if (item.GetType().Equals(typeof(CheckableAudioLoopbackDevice)))
                return (container as FrameworkElement).FindResource("LoopbackAudioSourceTemplate") as DataTemplate;
            else if (item.GetType().Equals(typeof(CheckableAudioProcessDevice)))
                return (container as FrameworkElement).FindResource("ProcessAudioSourceTemplate") as DataTemplate;
            else if (item.GetType().Equals(typeof(CheckableAudioCaptureDevice)))
                return (container as FrameworkElement).FindResource("CaptureAudioSourceTemplate") as DataTemplate;
            return null;
        }
    }
}
