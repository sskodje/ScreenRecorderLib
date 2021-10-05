#pragma once
using namespace System;
using namespace System::ComponentModel;

namespace ScreenRecorderLib {

	public ref class ScreenPoint : public INotifyPropertyChanged {
	internal:
		POINT ToPOINT() {
			POINT pt;
			pt.x = Left;
			pt.y = Top;
			return pt;
		}
	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		property double Left;
		property double Top;
		property static ScreenPoint^ Empty {
			ScreenPoint^ get() {
				return gcnew ScreenPoint(double::PositiveInfinity, double::PositiveInfinity);
			}
		}
		ScreenPoint() {}
		ScreenPoint(double left, double top) {
			Left = left;
			Top = top;
		}

		static bool operator== (ScreenPoint^ left, ScreenPoint^ right)
		{
			if (Object::ReferenceEquals(left, nullptr))
			{
				return Object::ReferenceEquals(right, nullptr);
			}
			else if (Object::ReferenceEquals(right, nullptr))
			{
				return Object::ReferenceEquals(left, nullptr);
			}
			return left->Left == right->Left
				&& left->Top == right->Top;
		}
		static bool operator!= (ScreenPoint^ left, ScreenPoint^ right)
		{
			return !(left == right);
		}
	};

	public ref class ScreenSize : public INotifyPropertyChanged {
	internal:
		SIZE ToSIZE() {
			SIZE size;
			size.cx = Width;
			size.cy = Height;
			return size;
		}
	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		property double Width;
		property double Height;
		property static ScreenSize^ Empty {
			ScreenSize^ get() {
				return gcnew ScreenSize(double::PositiveInfinity, double::PositiveInfinity);
			}
		}
		ScreenSize() {}
		ScreenSize(double width, double height) {
			Width = width;
			Height = height;
		}

		static bool operator== (ScreenSize^ left, ScreenSize^ right)
		{
			if (Object::ReferenceEquals(left, nullptr))
			{
				return Object::ReferenceEquals(right, nullptr);
			}
			else if (Object::ReferenceEquals(right, nullptr))
			{
				return Object::ReferenceEquals(left, nullptr);
			}
			return left->Width == right->Width
				&& left->Height == right->Height;
		}
		static bool operator!= (ScreenSize^ left, ScreenSize^ right)
		{
			return !(left == right);
		}
	};

	public ref class ScreenRect : public INotifyPropertyChanged {
	private:
		double _left;
		double _top;
		double _right;
		double _bottom;

	internal:
		RECT ToRECT() {
			RECT rect;
			rect.left = (LONG)round(Left);
			rect.top = (LONG)round(Top);
			rect.right = (LONG)round(Right);
			rect.bottom = (LONG)round(Bottom);
			return rect;
		}

	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		property double Left {
			double get() {
				return _left;
			}
			void set(double value) {
				_left = value;
				OnPropertyChanged("Left");
				OnPropertyChanged("Width");
			}
		}
		property double Top {
			double get() {
				return _top;
			}
			void set(double value) {
				_top = value;
				OnPropertyChanged("Top");
				OnPropertyChanged("Height");
			}
		}
		property double Right {
			double get() {
				return _right;
			}
			void set(double value) {
				_right = value;
				OnPropertyChanged("Right");
				OnPropertyChanged("Width");
			}
		}
		property double Bottom {
			double get() {
				return _bottom;
			}
			void set(double value) {
				_bottom = value;
				OnPropertyChanged("Bottom");
				OnPropertyChanged("Height");
			}
		}
		ScreenRect() {}
		ScreenRect(double left, double top, double width, double height) {
			Left = left;
			Top = top;
			if (double::IsPositiveInfinity(width)) {
				Right = double::NegativeInfinity;
			}
			else {
				Right = left + width;
			}
			if (double::IsPositiveInfinity(height)) {
				Bottom = double::NegativeInfinity;
			}
			else {
				Bottom = top + height;
			}
		}

		property double Width {
			double get() {
				return Right - Left;
			}
		}
		property double Height {
			double get() {
				return Bottom - Top;
			}
		}

		property static ScreenRect^ Empty {
			ScreenRect^ get() {
				return gcnew ScreenRect(double::PositiveInfinity, double::PositiveInfinity, double::NegativeInfinity, double::NegativeInfinity);
			}
		}
		static bool operator== (ScreenRect^ left, ScreenRect^ right)
		{
			if (Object::ReferenceEquals(left, nullptr))
			{
				return Object::ReferenceEquals(right, nullptr);
			}
			else if (Object::ReferenceEquals(right, nullptr))
			{
				return Object::ReferenceEquals(left, nullptr);
			}
			return left->Left == right->Left
				&& left->Top == right->Top
				&& left->Right == right->Right
				&& left->Bottom == right->Bottom;
		}
		static bool operator!= (ScreenRect^ left, ScreenRect^ right)
		{
			return !(left == right);
		}
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};
}