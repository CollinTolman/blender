/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bli
 *
 * Allows passing iterators over ranges of integers without actually allocating an array or passing
 * separate values. A range always has a step of one. If other step sizes are required in some
 * cases, a separate data structure should be used.
 */

#pragma once

#include <cmath>
#include <algorithm>

#include "BLI_utildefines.h"

#define RANGE_AS_ARRAY_REF_MAX_LEN 10000

namespace BLI {

template<typename T> class ArrayRef;

template<typename T> class Range {
 private:
  T m_start = 0;
  T m_one_after_last = 0;

 public:
  Range() = default;

  /**
   * Construct a new range.
   * Asserts when start is larger than one_after_last.
   */
  Range(T start, T one_after_last) : m_start(start), m_one_after_last(one_after_last)
  {
    BLI_assert(start <= one_after_last);
  }

  class Iterator {
   private:
    const Range &m_range;
    T m_current;

   public:
    Iterator(const Range &range, T current) : m_range(range), m_current(current)
    {
    }

    Iterator &operator++()
    {
      m_current++;
      return *this;
    }

    bool operator!=(const Iterator &iterator) const
    {
      return m_current != iterator.m_current;
    }

    T operator*() const
    {
      return m_current;
    }
  };

  Iterator begin() const
  {
    return Iterator(*this, m_start);
  }

  Iterator end() const
  {
    return Iterator(*this, m_one_after_last);
  }

  /**
   * Access an element in the range.
   */
  T operator[](uint index) const
  {
    BLI_assert(index < this->size());
    return m_start + index;
  }

  /**
   * Two ranges compare equal when they contain the same numbers.
   */
  friend bool operator==(Range<T> a, Range<T> b)
  {
    return a.m_start == b.m_start && a.m_one_after_last == b.m_one_after_last;
  }

  /**
   * Get the amount of numbers in the range.
   */
  uint size() const
  {
    return m_one_after_last - m_start;
  }

  /**
   * Create a new range starting at the end of the current one.
   */
  Range after(uint n) const
  {
    return Range(m_one_after_last, m_one_after_last + n);
  }

  /**
   * Create a new range that ends at the start of the current one.
   */
  Range before(uint n) const
  {
    return Range(m_start - n, m_start);
  }

  /**
   * Get the first element in the range.
   * Asserts when the range is empty.
   */
  T first() const
  {
    BLI_assert(this->size() > 0);
    return m_start;
  }

  /**
   * Get the last element in the range.
   * Asserts when the range is empty.
   */
  T last() const
  {
    BLI_assert(this->size() > 0);
    return m_one_after_last - 1;
  }

  /**
   * Get the element one after the end. The returned value is undefined when the range is empty.
   */
  T one_after_last() const
  {
    return m_one_after_last;
  }

  /**
   * Get the first element in the range. The returned value is undefined when the range is empty.
   */
  T start() const
  {
    return m_start;
  }

  /**
   * Returns true when the range contains a certain number, otherwise false.
   */
  bool contains(T value) const
  {
    return value >= m_start && value < m_one_after_last;
  }

  Range<T> slice(uint start, uint size) const
  {
    uint new_start = m_start + start;
    BLI_assert(new_start + size <= m_one_after_last || size == 0);
    return Range<T>(new_start, new_start + size);
  }

  /**
   * Get read-only access to a memory buffer that contains the range as actual numbers. This only
   * works for some ranges. The range must be within [0, RANGE_AS_ARRAY_REF_MAX_LEN].
   */
  ArrayRef<T> as_array_ref() const;
};

}  // namespace BLI
