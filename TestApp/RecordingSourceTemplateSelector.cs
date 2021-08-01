using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;

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

            return null;
        }
    }
}
