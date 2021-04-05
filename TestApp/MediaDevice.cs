using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Data;

namespace TestApp
{
    public class MediaDeviceToDeviceIdConverter : DependencyObject, IValueConverter
    {


        public List<MediaDevice> MediaDevices
        {
            get { return (List<MediaDevice>)GetValue(MediaDevicesProperty); }
            set { SetValue(MediaDevicesProperty, value); }
        }

        // Using a DependencyProperty as the backing store for MediaDevices.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty MediaDevicesProperty =
            DependencyProperty.Register("MediaDevices", typeof(List<MediaDevice>), typeof(MediaDeviceToDeviceIdConverter), new PropertyMetadata(null));


        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            MediaDevice device;
            if (value is null)
            {
                device = MediaDevices.FirstOrDefault(x => String.IsNullOrEmpty(x.ID));
            }
            else
            {
                device = MediaDevices.FirstOrDefault(x => value.ToString().Equals(x.ID));
            }
            return device;
        }
        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return (value as MediaDevice)?.ID;
        }
    }
    public class MediaDevice
    {
        public string ID { get; set; }
        public string Name { get; set; }
    }
}
