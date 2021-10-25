//          Copyright Maarten L. Hekkelman, 2020
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

/// A header-only library to compress arrays of increasing numbers of type
///	uint32_t.
///
/// This is a simplified version of the array compression routines in MRS
/// Only supported datatype is uint32_t and only supported width it 24 bit.

#pragma once

#include <cassert>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace sq
{

/// \brief the limits, store ints of at most 30 bits
const uint32_t kStartWidth = 8, kMaxWidth = 30;

/// \brief A simple wrapper around vector<uint8_t> to write out bits

class obitstream
{
  public:

    /// \brief the only constructor takes a \a buffer to store the actual data
    obitstream(std::vector<uint8_t>& buffer)
        : m_buffer(buffer)
    {
        m_buffer.push_back(0);
    }

    obitstream(const obitstream&) = delete;
    obitstream& operator=(const obitstream&) = delete;

    /// \brief write a single bit
	void write_bit(bool bit)
	{
		if (bit)
			m_buffer.back() |= 1 << m_bitOffset;
		
		if (--m_bitOffset < 0)
		{
			m_buffer.push_back(0);
			m_bitOffset = 7;
		}
	}

    friend obitstream& operator<<(obitstream& bs, bool bit);

	/// \brief write \a bits bits of the value in \a value
	void write(uint32_t value, int bits)
	{
		while (bits-- > 0)
		{
			if (value & (1UL << bits))
				m_buffer.back() |= 1 << m_bitOffset;
			
			if (--m_bitOffset < 0)
			{
				m_buffer.push_back(0);
				m_bitOffset = 7;
			}
		}
	}

    /// \brief flush out the remaining bits and write some padding
	void sync()
	{
		write_bit(0);

		while (m_bitOffset != 7)
			write_bit(1);
	}

    /// \brief peek into the data
	const uint8_t* data() const					{ return m_buffer.data(); }

    /// \brief the size in bytes of the data
	size_t size() const							{ return m_buffer.size(); }

	friend void write_delta_array(obitstream& bs, const std::vector<uint32_t>& data);

  private:
	std::vector<uint8_t>& m_buffer;
	int m_bitOffset = 7;
};

inline obitstream& operator<<(obitstream& bs, bool bit)
{
    bs.write_bit(bit);
    return bs;
}

/// \brief A simple wrapper around vector<uint8_t> to read bits

class ibitstream
{
  public:
    /// \brief constructor taking a const pointer to some stored data
	ibitstream(const uint8_t* data)
		: m_data(data), m_byte(*m_data++), m_bitOffset(7) {}

    /// \brief constructor using an obitstream as input
	ibitstream(const obitstream& bits)
		: ibitstream(bits.data()) {}

	ibitstream(const ibitstream&) = delete;
	ibitstream& operator=(const ibitstream&) = delete;

    /// \brief read a single value from the bitstream \a bc bits wide
	uint32_t read(int bc)
	{
		uint32_t result = 0;

		while (bc > 0)
		{
			static const uint8_t kM[] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF };

			int bw = m_bitOffset + 1;
			if (bw > bc)
				bw = bc;

			m_bitOffset -= bw;
			result = result << bw | (kM[bw] & (m_byte >> (m_bitOffset + 1)));

			if (m_bitOffset < 0)
			{
				m_byte = *m_data++;
				m_bitOffset = 7;
			}

			bc -= bw;
		}

		return result;
	}

    bool operator()() {
        return read(1);
    }

	friend std::vector<uint32_t> read_delta_array(ibitstream& bs);

    friend ibitstream& operator>>(ibitstream& bs, bool& bit);

  private:
	const uint8_t* m_data;
	uint8_t m_byte;
	int m_bitOffset;
};

inline ibitstream& operator>>(ibitstream& bs, bool& bit)
{
    bit = bs.read(1);
    return bs;
}

// --------------------------------------------------------------------
//	Basic routines for reading/writing numbers in bit streams
//
//	Binary mode writes a fixed bitcount number of bits for a value
//

/// \brief Read a value from \a bits with \a bit_count width
///
/// Simply reads \a bit_count bits and returns that as the result
/// \param bits			The bitstream containing the compressed data
/// \param bit_count	The number of bits to read
/// \result				The result cast to a uint32_t
inline uint32_t read_binary(ibitstream& bits, size_t bit_count)
{
	assert(bit_count <= sizeof(uint32_t) * 8);
	assert(bit_count > 0);

	return bits.read(bit_count);
}

/// \brief Write \a value to \a bits with \a bit_count width
///
/// Simply writes the lower \a bit_count bits from \a value into \a bits
/// \param bits			The bitstream containing the compressed data
/// \param bit_count	The number of bits to write
/// \param value		The value whose bits to write
inline void write_binary(obitstream& bits, size_t bit_count, uint32_t value)
{
	assert(bit_count <= 64);
	assert(bit_count > 0);

	while (bit_count-- > 0)
		bits << (value & (1ULL << bit_count));
}

///	Gamma mode writes a variable number of bits for a value, optimal for small numbers.
/// The algorithm writes out as many 1 bits as the width needed to store the value v
/// followed by a 0 bit to indicate the end. This number of 1 bits is called e.
/// Now take the value v1 = (1 << e) and subtract this from v resulting in v2.
/// This v2 is then written out in e bits.

/// \brief Read a value of at most 32 bits wide from \a bits
///
/// See explanation above, reads a value from \a bits and returns the result
/// \param bits		The bits stream
/// \result			The value read cast to a uint32_t

inline uint32_t read_gamma(ibitstream& bits)
{
	uint32_t v1 = 1;
	int32_t e = 0;

	while (bits() and v1 != 0)
	{
		v1 <<= 1;
		++e;
	}

	assert(v1 != 0);

	uint32_t v2 = 0;
	while (e-- > 0)
		v2 = (v2 << 1) | bits();

	return v1 + v2;
}

/// \brief Write \a value to \a bits
///
/// See explanation above, reads a value from \a bits and returns the result
/// \param bits		The bits stream
/// \param value	The value to write

inline void write_gamma(obitstream& bits, uint32_t value)
{
	assert(value > 0);

	uint32_t v = value;
	int32_t e = 0;

	while (v > 1)
	{
		v >>= 1;
		++e;
		bits << 1;
	}
	bits << 0;

	uint64_t b = 1ULL << e;
	while (e-- > 0)
	{
		b >>= 1;
		bits << (value & b);
	}
}

// --------------------------------------------------------------------

namespace detail
{

/// The algorithm for storing compressed arrays uses the selector based
/// algorithm. Sorry, I lost the source of the original idea.
/// --------------------------------------------------------------------
/// The idea is to use a selector code of 4 bits and a state of the
/// current width to use. Based on the selector code, the next 1 to 4
/// values are stored with the adjusted width. Selector 0 indicates a
/// widening of the width to max_width.

struct selector
{
	int32_t databits;
	uint32_t span;
} const kSelectors[16] = {
	{  0, 1 },
	{ -4, 1 },
	{ -2, 1 }, { -2, 2 },
	{ -1, 1 }, { -1, 2 }, { -1, 4 },
	{  0, 1 }, {  0, 2 }, {  0, 4 },
	{  1, 1 }, {  1, 2 }, {  1, 4 },
	{  2, 1 }, {  2, 2 },
	{  4, 1 }
};
 
/// \brief return the width in bits of the value \a v
inline uint32_t bit_width(uint32_t v)
{
	uint32_t result = 0;
	while (v > 0)
	{
		v >>= 1;
		++result;
	}
	return result;
}

inline void compress_simple_array_selector(obitstream& bits, const std::vector<uint32_t>& arr)
{
	int32_t width = kStartWidth;

	int32_t bn[4];
	uint32_t dv[4];
	uint32_t bc = 0;
	auto a = arr.begin(), e = arr.end();

	while (a != e or bc > 0)
	{
		while (bc < 4 and a != e)
		{
			dv[bc] = *a++;
			bn[bc] = bit_width(dv[bc]);
			++bc;
		}

		uint32_t s = 0;
		int32_t c = bn[0] - kMaxWidth;

		for (uint32_t i = 1; i < 16; ++i)
		{
			if (kSelectors[i].span > bc)
				continue;

			int32_t w = width + kSelectors[i].databits;

			if (static_cast<uint32_t>(w) > kMaxWidth)
				continue;

			bool fits = true;
			int32_t waste = 0;

			switch (kSelectors[i].span)
			{
				case 4:
					fits = fits and bn[3] <= w;
					waste += w - bn[3];
					[[fallthrough]]
				case 3:
					fits = fits and bn[2] <= w;
					waste += w - bn[2];
					[[fallthrough]]
				case 2:
					fits = fits and bn[1] <= w;
					waste += w - bn[1];
					[[fallthrough]]
				case 1:
					fits = fits and bn[0] <= w;
					waste += w - bn[0];
			}

			if (fits == false)
				continue;

			int32_t n = (kSelectors[i].span - 1) * 4 - waste;

			if (n > c)
			{
				s = i;
				c = n;
			}
		}

		if (s == 0)
			width = kMaxWidth;
		else
			width += kSelectors[s].databits;

		uint32_t n = kSelectors[s].span;

		bits.write(s, 4);

		if (width > 0)
		{
			for (uint32_t i = 0; i < n; ++i)
				bits.write(dv[i], width);
		}

		bc -= n;

		if (bc > 0)
		{
			for (uint32_t i = 0; i < (4 - n); ++i)
			{
				bn[i] = bn[i + n];
				dv[i] = dv[i + n];
			}
		}
	}
}

}

// --------------------------------------------------------------------
//	Arrays are a bit more complex

/// \brief Read an array of delta values from a compressed bit stream
///
/// An array of delta's is an array that consists of very small
/// numbers. Usually constructed using the difference between
/// two consecutive values in an ordered, unique list.
/// This routine unpacks the compressed array and returns the result.
///
/// \param bits	The bit stream containing the data
/// \result 	The decompressed array

inline std::vector<uint32_t> read_delta_array(ibitstream& bits)
{
	uint32_t size = read_gamma(bits);
	
    std::vector<uint32_t> result(size);

	uint32_t width = kStartWidth;
	uint32_t span = 0;
	
	for (auto& v: result)
	{
		if (span == 0)
		{
			uint32_t selector = bits.read(4);
			span = detail::kSelectors[selector].span;

			if (selector == 0)
				width = kMaxWidth;
			else
				width += detail::kSelectors[selector].databits;
		}

		if (width > 0)
			v = bits.read(width);
		else
			v = 0;

		--span;
	}

	return result;
}

/// \brief Write out an array of delta values to a compressed bit stream
///
/// An array of delta's is an array that consists of very small
/// numbers. Usually constructed using the difference between
/// two consecutive values in an ordered, unique list.
///
/// The values in \a arr should never be zero.
///
/// \param bits	The bit vector to store the bits
/// \param arr	The vector containing the delta's

inline void write_delta_array(obitstream& bits, const std::vector<uint32_t>& arr)
{
	uint32_t cnt = static_cast<uint32_t>(arr.size());
	write_gamma(bits, cnt);
	detail::compress_simple_array_selector(bits, arr);
}

// --------------------------------------------------------------------

/// \brief Read an array of increasing values from a compressed bit stream
///
/// This routine decompresses the array stored in bits. The result will be
/// an array containing values that are increasing in value and always unique.
///
/// \param bits	The bit stream containing the data
/// \result 	The decompressed array

inline std::vector<uint32_t> read_array(ibitstream& bits)
{
	auto result = read_delta_array(bits);

	uint32_t last = static_cast<uint32_t>(-1);
	for (auto& v: result)
		last = v = v + last + 1;

	return result;
}

/// \brief Write out an array of unique, increasing values to a compressed bit stream
///
/// The values in \a arr should be unique, increasing in value and may start with zero.
///
/// \param bits	The bit vector to store the bits
/// \param arr	The vector containing the delta's

inline void write_array(obitstream& bits, const std::vector<uint32_t>& arr)
{
	std::vector<uint32_t> deltas;
	deltas.reserve(arr.size());

	if (not arr.empty())
	{
		// overflow might occur, but will be resolved
		uint32_t last = static_cast<uint32_t>(-1);
		for (auto& v: arr)
		{
			assert(v > last or last == static_cast<uint32_t>(-1));
			deltas.push_back(v - last - 1);
			last = v;
		}
	}

	write_delta_array(bits, deltas);
}

}