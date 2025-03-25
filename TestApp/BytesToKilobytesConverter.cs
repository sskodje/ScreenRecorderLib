using System;
using System.Globalization;
using System.Windows.Data;

namespace TestApp
{
    public class BytesToKilobytesConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return System.Convert.ToInt64(value) / 1000;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return System.Convert.ToInt64(value) * 1000;
        }
    }
}
