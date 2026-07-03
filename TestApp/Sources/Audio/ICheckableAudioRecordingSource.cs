using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace TestApp.Sources.Audio
{
    public interface ICheckableAudioRecordingSource : INotifyPropertyChanged
    {
        string ID { get; }
        bool IsSelected { get; set; }
        bool IsCheckable { get; set; }
        double Gain { get; set; }
    }
}
