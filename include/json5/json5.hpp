#pragma once

#include <cstdint>
#include <fstream>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace json5::detail {

using string_offset = unsigned;

} // namespace json5::detail

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace json5 {

class value;

enum class value_type { null = 0, boolean, number, array, string, object };

using values_t = std::vector<value>;

class value final
{
public:
	value() noexcept = default;
	value( std::nullptr_t ) noexcept : _data( type_null ) { }
	value( bool val ) noexcept : _data( val ? type_true : type_false ) { }
	value( int val ) noexcept : _double( static_cast<double>( val ) ) { }
	value( float val ) noexcept : _double( static_cast<double>( val ) ) { }
	value( double val ) noexcept : _double( val ) { }

	value_type type() const noexcept;

	bool is_null() const noexcept { return _data == type_null; }
	bool is_boolean() const noexcept { return _data == type_true || _data == type_false; }
	bool is_number() const noexcept { return ( _data & mask_nanbits ) != mask_nanbits; }
	bool is_string() const noexcept { return ( _data & mask_type ) == type_string; }
	bool is_object() const noexcept { return ( _data & mask_type ) == type_object; }
	bool is_array() const noexcept { return ( _data & mask_type ) == type_array; }

	bool get_bool( bool val = false ) const noexcept;
	int get_int( int val = 0 ) const noexcept { return is_number() ? static_cast<int>( _double ) : val; }
	int64_t get_int64( int64_t val = 0 ) const noexcept { return is_number() ? static_cast<int64_t>( _double ) : val; }
	unsigned get_uint( unsigned val = 0 ) const noexcept { return is_number() ? static_cast<unsigned>( _double ) : val; }
	uint64_t get_uint64( int64_t val = 0 ) const noexcept { return is_number() ? static_cast<uint64_t>( _double ) : val; }
	size_t get_size_t( size_t val = 0 ) const noexcept { return is_number() ? static_cast<size_t>( _double ) : val; }
	float get_float( float val = 0.0f ) const noexcept { return is_number() ? static_cast<float>( _double ) : val; }
	double get_double( double val = 0.0 ) const noexcept { return is_number() ? _double : val; }
	const char *get_c_str( const char *val = "" ) const noexcept;

	bool operator==( const value &other ) const noexcept;
	bool operator!=( const value &other ) const noexcept { return !( ( *this ) == other ); }

	values_t operator()( std::string_view pattern ) const noexcept;

	template <typename T>
	T payload() const noexcept { return ( T )( _data & mask_payload ); }

private:
	value( value_type t, uint64_t data );
	value( value_type t, const void *data ) : value( t, reinterpret_cast<uint64_t>( data ) ) { }

	void relink( const class document *prevDoc, const class document &doc ) noexcept;

	// NaN-boxed data
	union
	{
		double _double;
		uint64_t _data;
	};

	static constexpr uint64_t mask_nanbits = 0xFFF0000000000000ull;
	static constexpr uint64_t mask_type    = 0xFFFF000000000000ull;
	static constexpr uint64_t mask_payload = 0x0000FFFFFFFFFFFFull;
	static constexpr uint64_t type_null    = 0xFFFC000000000000ull;
	static constexpr uint64_t type_false   = 0xFFF1000000000000ull;
	static constexpr uint64_t type_true    = 0xFFF3000000000000ull;
	static constexpr uint64_t type_string  = 0xFFF2000000000000ull;
	static constexpr uint64_t type_array   = 0xFFF4000000000000ull;
	static constexpr uint64_t type_object  = 0xFFF6000000000000ull;

	void payload( uint64_t p ) noexcept { _data = ( _data & ~mask_payload ) | p; }
	void payload( const void *p ) noexcept { payload( reinterpret_cast<uint64_t>( p ) ); }

	friend class document;
	friend class builder;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct error final
{
	enum
	{
		none,
		invalid_root,
		unexpected_end,
		syntax_error,
		invalid_literal,
		invalid_escape_seq,
		comma_expected,
		colon_expected,
		boolean_expected,
		number_expected,
		string_expected,
		object_expected,
		array_expected,
		wrong_array_size
	};

	int type = none;
	int line = 0;
	int column = 0;

	operator int() const noexcept { return type; }
};

} // namespace json5

#include "json5_views.inl"
#include "json5_filter.inl"
#include "json5_document.inl"
#include "json5_builder.inl"
#include "json5_reader.inl"

namespace json5 {

//---------------------------------------------------------------------------------------------------------------------
value::value( value_type t, uint64_t data )
{
	if ( t == value_type::object )
		_data = type_object | data;
	else if ( t == value_type::array )
		_data = type_array | data;
	else if ( t == value_type::string )
		_data = type_string | data;
	else
		_data = type_null;
}

//---------------------------------------------------------------------------------------------------------------------
inline value_type value::type() const noexcept
{
	if ( ( _data & mask_nanbits ) != mask_nanbits )
		return value_type::number;

	if ( ( _data & mask_type ) == type_object )
		return value_type::object;
	else if ( ( _data & mask_type ) == type_array )
		return value_type::array;
	else if ( ( _data & mask_type ) == type_string )
		return value_type::string;
	if ( _data == type_true || _data == type_false )
		return value_type::boolean;

	return value_type::null;
}

//---------------------------------------------------------------------------------------------------------------------
inline bool value::get_bool( bool val ) const noexcept
{
	if ( _data == type_true )
		return true;
	else if ( _data == type_false )
		return false;

	return val;
}

//---------------------------------------------------------------------------------------------------------------------
inline const char *value::get_c_str( const char *defaultValue ) const noexcept
{
	return is_string() ? reinterpret_cast<const char *>( _data & mask_payload ) : defaultValue;
}

//---------------------------------------------------------------------------------------------------------------------
inline bool value::operator==( const value &other ) const noexcept
{
	if ( auto t = type(); t == other.type() )
	{
		if ( t == value_type::null )
			return true;
		else if ( t == value_type::boolean )
			return _data == other._data;
		else if ( t == value_type::number )
			return _double == other._double;
		else if ( t == value_type::string )
			return !strcmp(
			           reinterpret_cast<const char *>( _data & mask_payload ),
			           reinterpret_cast<const char *>( other._data & mask_payload ) );
		else if ( t == value_type::array )
			return array_view( *this ) == array_view( other );
		else if ( t == value_type::object )
			return object_view( *this ) == object_view( other );
	}

	return false;
}

//---------------------------------------------------------------------------------------------------------------------
inline values_t value::operator()( std::string_view pattern ) const noexcept
{
	values_t result;
	detail::filter( *this, pattern, result );
	return result;
}

//---------------------------------------------------------------------------------------------------------------------
inline void value::relink( const class document *prevDoc, const class document &doc ) noexcept
{
	if ( is_string() )
	{
		if ( prevDoc )
			payload( payload<const char *>() - prevDoc->_strings.data() );

		payload( doc._strings.data() + payload<uint64_t>() );
	}
	else if ( is_object() || is_array() )
	{
		if ( prevDoc )
			payload( payload<const value *>() - prevDoc->_values.data() );

		payload( doc._values.data() + payload<uint64_t>() );
	}
}

//---------------------------------------------------------------------------------------------------------------------
inline void to_stream( std::ostream &os, const error &err )
{
	const char *errStrings[] =
	{
		"none", "invalid root", "unexpected end", "syntax error", "invalid literal",
		"invalid escape sequence", "comma expected", "colon expected", "boolean expected",
		"number expected", "string expected", "object expected", "array expected", "wrong array size"
	};

	os << errStrings[err.type] << " at " << err.line << ":" << err.column;
}

//---------------------------------------------------------------------------------------------------------------------
inline std::string to_string( const error &err )
{
	std::ostringstream os;
	to_stream( os, err );
	return os.str();
}

//---------------------------------------------------------------------------------------------------------------------
struct output_style
{
	const char *indentation = "  ";
	bool json_compatible = false;
	bool ignore_defaults = false;
};

//---------------------------------------------------------------------------------------------------------------------
inline void to_stream( std::ostream &os, const char *str )
{
	os << "\"";

	while ( *str )
	{
		if ( str[0] == '\n' )
			os << "\\n";
		else if ( str[0] == '\r' )
			os << "\\r";
		else if ( str[0] == '\t' )
			os << "\\t";
		else if ( str[0] == '"' )
			os << "\\\"";
		else if ( str[0] == '\\' )
			os << "\\\\";
		else
			os << *str;

		++str;
	}

	os << "\"";
}

//---------------------------------------------------------------------------------------------------------------------
inline void to_stream( std::ostream &os, const value &v, const output_style &style = output_style(), int depth = 0 )
{
	if ( v.is_null() )
		os << "null";
	else if ( v.is_boolean() )
		os << ( v.get_bool() ? "true" : "false" );
	else if ( v.is_number() )
	{
		if ( double _, d = v.get_double(); modf( d, &_ ) == 0.0 )
			os << v.get_int64();
		else
			os << d;
	}
	else if ( v.is_string() )
		to_stream( os, v.get_c_str() );
	else if ( v.is_array() )
	{
		if ( auto av = json5::array_view( v ); !av.empty() )
		{
			os << "[" << std::endl;
			for ( size_t i = 0, S = av.size(); i < S; ++i )
			{
				for ( int i = 0; i <= depth; ++i ) os << style.indentation;
				to_stream( os, av[i], style, depth + 1 );
				if ( i < S - 1 ) os << ",";
				os << std::endl;
			}

			for ( int i = 0; i < depth; ++i ) os << style.indentation;
			os << "]";
		}
		else
			os << "[]";
	}
	else if ( v.is_object() )
	{
		if ( auto ov = json5::object_view( v ); !ov.empty() )
		{
			os << "{" << std::endl;
			size_t count = ov.size();
			for ( auto kvp : ov )
			{
				for ( int i = 0; i <= depth; ++i ) os << style.indentation;

				if ( style.json_compatible )
					os << "\"" << kvp.first << "\": ";
				else
					os << kvp.first << ": ";

				to_stream( os, kvp.second, style, depth + 1 );
				if ( --count ) os << ",";
				os << std::endl;
			}

			for ( int i = 0; i < depth; ++i ) os << style.indentation;
			os << "}";
		}
		else
			os << "{}";
	}

	if ( !depth )
		os << std::endl;
}

//---------------------------------------------------------------------------------------------------------------------
inline void to_stream( std::ostream &os, const document &doc, const output_style &style = output_style() )
{
	to_stream( os, doc.root(), style, 0 );
}

//---------------------------------------------------------------------------------------------------------------------
inline void to_string( std::string &str, const document &doc, const output_style &style = output_style() )
{
	std::ostringstream os;
	to_stream( os, doc, style );
	str = os.str();
}

//---------------------------------------------------------------------------------------------------------------------
inline std::string to_string( const document &doc, const output_style &style = output_style() )
{
	std::string result;
	to_string( result, doc, style );
	return result;
}

//---------------------------------------------------------------------------------------------------------------------
inline bool to_file( const std::string &fileName, const document &doc, const output_style &style = output_style() )
{
	std::ofstream ofs( fileName );
	to_stream( ofs, doc, style );
	return true;
}

//---------------------------------------------------------------------------------------------------------------------
inline error from_stream( std::istream &is, document &doc )
{
	detail::reader r{ doc, is };
	return r.parse();
}

//---------------------------------------------------------------------------------------------------------------------
inline error from_string( const std::string &str, document &doc )
{
	std::istringstream is( str );
	return from_stream( is, doc );
}

//---------------------------------------------------------------------------------------------------------------------
inline error from_file( const std::string &fileName, document &doc )
{
	std::ifstream ifs( fileName );
	return from_stream( ifs, doc );
}

} // namespace json5

#include "json5_reflect.inl"
