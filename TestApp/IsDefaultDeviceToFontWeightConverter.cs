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
    public class IsDefaultDeviceToFontWeightConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            bool isDefault = (bool)value;
            return isDefault ? FontWeights.Bold : FontWeights.Normal;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            FontWeight weight = (FontWeight)value;
            if (weight == FontWeights.Bold)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
