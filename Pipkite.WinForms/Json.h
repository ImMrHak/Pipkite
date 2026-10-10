#pragma once
// JSON helpers over System.Web.Script.Serialization.JavaScriptSerializer (.NET Framework 4.8).
// int64 identifiers travel as strings; numbers are read through Convert to tolerate Int32/Int64/Decimal.

using namespace System;
using namespace System::Collections;
using namespace System::Collections::Generic;
using namespace System::Web::Script::Serialization;

namespace PipkiteUI {

typedef Dictionary<String^, Object^> JObj;

public ref class J abstract sealed {
public:
    static J() {
        serializer = gcnew JavaScriptSerializer();
        serializer->MaxJsonLength = Int32::MaxValue;
        serializer->RecursionLimit = 256;
    }

    static Object^ Parse(String^ s) { return String::IsNullOrEmpty(s) ? nullptr : serializer->DeserializeObject(s); }
    static String^ Stringify(Object^ o) { return serializer->Serialize(o); }

    static JObj^ New() { return gcnew JObj(); }
    static JObj^ Obj(Object^ o) { return dynamic_cast<JObj^>(o); }
    static JObj^ Obj(JObj^ d, String^ k) {
        Object^ v;
        return (d != nullptr && d->TryGetValue(k, v)) ? dynamic_cast<JObj^>(v) : nullptr;
    }
    static array<Object^>^ Arr(Object^ o) {
        array<Object^>^ a = dynamic_cast<array<Object^>^>(o);
        if (a != nullptr) return a;
        ArrayList^ l = dynamic_cast<ArrayList^>(o);
        if (l != nullptr) return l->ToArray();
        return gcnew array<Object^>(0);
    }
    static array<Object^>^ Arr(JObj^ d, String^ k) {
        Object^ v;
        return (d != nullptr && d->TryGetValue(k, v)) ? Arr(v) : gcnew array<Object^>(0);
    }
    static Object^ Get(JObj^ d, String^ k) {
        Object^ v;
        return (d != nullptr && d->TryGetValue(k, v)) ? v : nullptr;
    }
    static bool Has(JObj^ d, String^ k) {
        Object^ v;
        return d != nullptr && d->TryGetValue(k, v) && v != nullptr;
    }
    static String^ Str(JObj^ d, String^ k) { return Str(d, k, ""); }
    static String^ Str(JObj^ d, String^ k, String^ def) {
        Object^ v = Get(d, k);
        if (v == nullptr) return def;
        String^ s = dynamic_cast<String^>(v);
        if (s != nullptr) return s;
        if (dynamic_cast<Boolean^>(v) != nullptr) return safe_cast<bool>(v) ? "true" : "false";
        return Convert::ToString(v, Globalization::CultureInfo::InvariantCulture);
    }
    static double Num(JObj^ d, String^ k) { return Num(d, k, 0.0); }
    static double Num(JObj^ d, String^ k, double def) {
        Object^ v = Get(d, k);
        if (v == nullptr) return def;
        try {
            return Convert::ToDouble(v, Globalization::CultureInfo::InvariantCulture);
        } catch (Exception^) {
            return def;
        }
    }
    static Int64 Int(JObj^ d, String^ k) {
        Object^ v = Get(d, k);
        if (v == nullptr) return 0;
        try {
            return Convert::ToInt64(v, Globalization::CultureInfo::InvariantCulture);
        } catch (Exception^) {
            return 0;
        }
    }
    static bool Bool(JObj^ d, String^ k) { return Bool(d, k, false); }
    static bool Bool(JObj^ d, String^ k, bool def) {
        Object^ v = Get(d, k);
        if (v == nullptr) return def;
        Boolean^ b = dynamic_cast<Boolean^>(v);
        if (b != nullptr) return *b;
        String^ s = dynamic_cast<String^>(v);
        return s != nullptr ? s == "true" : def;
    }
    static String^ Fmt(double v, int digits) { return v.ToString("F" + digits, Globalization::CultureInfo::InvariantCulture); }
    static String^ Fmt(double v) { return v.ToString("0.#####", Globalization::CultureInfo::InvariantCulture); }
    static bool TryNum(String^ s, double% out) {
        return Double::TryParse(s == nullptr ? "" : s->Trim(), Globalization::NumberStyles::Float, Globalization::CultureInfo::InvariantCulture, out);
    }
    static String^ Time(Int64 ms) {
        if (ms <= 0) return "";
        return DateTimeOffset::FromUnixTimeMilliseconds(ms).ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss");
    }
    static String^ ShortTime(Int64 ms) {
        if (ms <= 0) return "";
        return DateTimeOffset::FromUnixTimeMilliseconds(ms).ToLocalTime().ToString("HH:mm:ss");
    }
    static List<Object^>^ List() { return gcnew System::Collections::Generic::List<Object^>(); }

private:
    static JavaScriptSerializer^ serializer;
};

}  // namespace PipkiteUI
