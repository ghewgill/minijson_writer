#ifndef MINIJSON_WRITER_H
#define MINIJSON_WRITER_H

#include <cstddef>
#include <string>
#include <iterator>
#include <ostream>
#include <iomanip>
#include <locale>

#define MJW_CPP11_SUPPORTED __cplusplus > 199711L || _MSC_VER >= 1800

#if MJW_CPP11_SUPPORTED

#include <type_traits>
#include <cmath>
#define MJW_LIB_NS std
#define MJW_ISFINITE(X) std::isfinite(X)

#else

#include <boost/type_traits.hpp>
#include <boost/math/special_functions/fpclassify.hpp>
#define MJW_LIB_NS boost
#define MJW_ISFINITE(X) boost::math::isfinite(X)

#endif

namespace minijson
{

enum null_t
{
    null = 0
};

template<typename V, typename Enable = void>
struct default_value_writer;

template<typename InputIt>
void write_array(std::ostream& stream, InputIt begin, InputIt end);

template<typename InputIt, typename ValueWriter>
void write_array(std::ostream& stream, InputIt begin, InputIt end, ValueWriter value_writer);

namespace detail
{

template<bool>
struct enable_if;

template<>
struct enable_if<true>
{
    typedef void type;
};

template<typename InputIt>
struct get_value_type
{
    typedef typename MJW_LIB_NS::remove_cv<typename std::iterator_traits<InputIt>::value_type>::type type;
};

template<size_t Size = 128>
class buffered_writer
{
private:

    std::ostream& m_stream;
    char m_buffer[Size];
    size_t m_offset;

public:

    explicit buffered_writer(std::ostream& stream) :
        m_stream(stream),
        m_offset(0)
    {
    }

    buffered_writer& operator<<(char c)
    {
        if (m_offset == Size)
        {
            flush();
        }

        m_buffer[m_offset++] = c;

        return *this;
    }

    template<size_t N>
    buffered_writer& operator<<(const char (&str)[N])
    {
        for (size_t i = 0; i < N - 1; i++)
        {
            operator<<(str[i]);
        }

        return *this;
    }

    void flush()
    {
        m_stream.write(m_buffer, m_offset);

        m_offset = 0;
    }
};

namespace
{

void adjust_stream_settings(std::ostream& stream)
{
    stream.imbue(std::locale::classic());
    stream << std::resetiosflags(std::ios::showpoint | std::ios::showpos);
    stream << std::dec << std::setw(0);
}

void write_quoted_string(std::ostream& stream, const char* str)
{
    stream << std::hex << std::right << std::setfill('0');

    buffered_writer<> writer(stream);

    writer << '"';

    while (*str != '\0')
    {
        switch (*str)
        {
        case '"':
            writer << "\\\"";
            break;

        case '\\':
            writer << "\\\\";
            break;

        case '\n':
            writer << "\\n";
            break;

        case '\r':
            writer << "\\r";
            break;

        case '\t':
            writer << "\\t";
            break;

        default:
            if ((*str > 0 && *str < 32) || *str == 127) // ASCII control characters (NUL is not supported)
            {
                writer << "\\u";

                writer.flush();
                stream << std::setw(4) << static_cast<unsigned>(*str);
            }
            else
            {
                writer << *str;
            }
            break;
        }
        str++;
    }

    writer << '"';

    writer.flush();

    stream << std::dec;
}

} // unnamed namespace

template<typename InputIt>
struct range
{
    InputIt begin;
    InputIt end;
};

template<typename InputIt>
range<InputIt> make_range(InputIt begin, InputIt end)
{
    const range<InputIt> range = { begin, end };

    return range;
}

template<typename InputIt, typename ValueWriter>
class range_writer
{
private:

    ValueWriter m_value_writer;

public:

    explicit range_writer(ValueWriter value_writer) :
        m_value_writer(value_writer)
    {
    }

    void operator()(std::ostream& stream, const range<InputIt>& range) const
    {
        write_array(stream, range.begin, range.end, m_value_writer);
    }
};

} // namespace detail

class writer
{
private:

    enum status
    {
        EMPTY,
        OPEN,
        CLOSED
    };

    bool m_array;
    status m_status;
    std::ostream* m_stream;

    void write_opening_bracket()
    {
        if (m_array)
        {
            *m_stream << '[';
        }
        else
        {
            *m_stream << '{';
        }
    }

    void write_closing_bracket()
    {
        if (m_array)
        {
            *m_stream << ']';
        }
        else
        {
            *m_stream << '}';
        }
    }

protected:

    void next_field()
    {
        if (m_status == EMPTY)
        {
            write_opening_bracket();
        }
        else if (m_status == OPEN)
        {
            *m_stream << ',';
        }

        m_status = OPEN;
    }

    void write_field_name(const char* name)
    {
        detail::write_quoted_string(*m_stream, name);

        *m_stream << ':';
    }

    template<typename V, typename ValueWriter>
    void write_helper(const char* field_name, const V& value, ValueWriter value_writer)
    {
        if (m_status == CLOSED)
        {
            return;
        }

        detail::adjust_stream_settings(*m_stream);

        next_field();

        if (field_name != NULL)
        {
            write_field_name(field_name);
        }

        value_writer(*m_stream, value);
    }

    explicit writer(std::ostream& stream, bool array) :
        m_array(array),
        m_status(EMPTY),
        m_stream(&stream)
    {
    }

public:

    std::ostream& stream() const
    {
        return *m_stream;
    }

    void close()
    {
        if (m_status == CLOSED)
        {
            return;
        }

        detail::adjust_stream_settings(*m_stream);

        if (m_status == EMPTY)
        {
            write_opening_bracket();
        }

        write_closing_bracket();

        m_status = CLOSED;
    }

};

class object_writer;
class array_writer;

class object_writer : public writer
{
public:

    explicit object_writer(std::ostream& stream) : writer(stream, false)
    {
    }

    template<typename V>
    void write(const char* field_name, const V& value)
    {
        write_helper(field_name, value, default_value_writer<V>());
    }

    template<typename V, typename ValueWriter>
    void write(const char* field_name, const V& value, ValueWriter value_writer)
    {
        write_helper(field_name, value, value_writer);
    }

    template<typename InputIt>
    void write_array(const char* field_name, InputIt begin, InputIt end)
    {
        write_array(field_name, begin, end, default_value_writer<typename detail::get_value_type<InputIt>::type>());
    }

    template<typename InputIt, typename ValueWriter>
    void write_array(const char* field_name, InputIt begin, InputIt end, ValueWriter value_writer)
    {
        write(field_name, detail::make_range(begin, end), detail::range_writer<InputIt, ValueWriter>(value_writer));
    }

    object_writer nested_object(const char* field_name);

    array_writer nested_array(const char* field_name);

};

class array_writer : public writer
{
public:

    explicit array_writer(std::ostream& stream) : writer(stream, true)
    {
    }

    template<typename V>
    void write(const V& value)
    {
        write_helper(NULL, value, default_value_writer<V>());
    }

    template<typename V, typename ValueWriter>
    void write(const V& value, ValueWriter value_writer)
    {
        write_helper(NULL, value, value_writer);
    }

    template<typename InputIt>
    void write_array(InputIt begin, InputIt end)
    {
        write_array(begin, end, default_value_writer<typename detail::get_value_type<InputIt>::type>());
    }

    template<typename InputIt, typename ValueWriter>
    void write_array(InputIt begin, InputIt end, ValueWriter value_writer)
    {
        write(detail::make_range(begin, end), detail::range_writer<InputIt, ValueWriter>(value_writer));
    }

    object_writer nested_object();

    array_writer nested_array();

};

inline object_writer object_writer::nested_object(const char* field_name)
{
    detail::adjust_stream_settings(stream());

    next_field();
    write_field_name(field_name);

    return object_writer(stream());
}

inline array_writer object_writer::nested_array(const char* field_name)
{
    detail::adjust_stream_settings(stream());

    next_field();
    write_field_name(field_name);

    return array_writer(stream());
}

inline object_writer array_writer::nested_object()
{
    detail::adjust_stream_settings(stream());

    next_field();

    return object_writer(stream());
}

inline array_writer array_writer::nested_array()
{
    detail::adjust_stream_settings(stream());

    next_field();

    return array_writer(stream());
}

template<>
struct default_value_writer<null_t>
{
    void operator()(std::ostream& stream, null_t) const
    {
        stream << "null";
    }
};

#if MJW_CPP11_SUPPORTED
template<>
struct default_value_writer<std::nullptr_t>
{
    void operator()(std::ostream& stream, std::nullptr_t) const
    {
        default_value_writer<null_t>()(stream, null);
    }
};
#endif

template<typename IntegralType>
struct default_value_writer<
        IntegralType,
        typename detail::enable_if<MJW_LIB_NS::is_integral<IntegralType>::value && !MJW_LIB_NS::is_same<IntegralType, bool>::value>::type>
{
    void operator()(std::ostream& stream, IntegralType value) const
    {
        // the unary plus is used here to force chars to be printed as integers
        stream << +value;
    }
};

template<>
struct default_value_writer<bool>
{
    void operator()(std::ostream& stream, bool value) const
    {
        if (value)
        {
            stream << "true";
        }
        else
        {
            stream << "false";
        }
    }
};

template<typename FloatingPoint>
struct default_value_writer<
        FloatingPoint,
        typename detail::enable_if<MJW_LIB_NS::is_floating_point<FloatingPoint>::value>::type>
{
    void operator()(std::ostream& stream, FloatingPoint value) const
    {
        // Numeric values that cannot be represented as sequences of digits
        // (such as Infinity and NaN) are not permitted in JSON
        if (!MJW_ISFINITE(value))
        {
            default_value_writer<null_t>()(stream, null); // falling back to null
        }
        else
        {
            stream << value;
        }
    }
};

template<>
struct default_value_writer<char*>
{
    void operator()(std::ostream& stream, const char* str) const
    {
        detail::write_quoted_string(stream, str);
    }
};

template<>
struct default_value_writer<const char*> : public default_value_writer<char*>
{
};

template<size_t N>
struct default_value_writer<char[N]> : public default_value_writer<char*>
{
};

template<size_t N>
struct default_value_writer<const char[N]> : public default_value_writer<char*>
{
};

template<>
struct default_value_writer<std::string>
{
    void operator()(std::ostream& stream, const std::string& str) const
    {
        default_value_writer<char*>()(stream, str.c_str());
    }
};

template<typename InputIt>
void write_array(std::ostream& stream, InputIt begin, InputIt end)
{
    write_array(stream, begin, end, default_value_writer<typename detail::get_value_type<InputIt>::type>());
}

template<typename InputIt, typename ValueWriter>
void write_array(std::ostream& stream, InputIt begin, InputIt end, ValueWriter value_writer)
{
    array_writer writer(stream);

    for (InputIt it = begin; it != end; ++it)
    {
        writer.write(*it, value_writer);
    }

    writer.close();
}

} // namespace minijson

#endif // MINIJSON_WRITER_H
