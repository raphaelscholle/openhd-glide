/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * © OpenHD, All Rights Reserved.
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace glide::flow {

struct VectorPoint {
    float x {};
    float y {};
};

struct VectorStroke {
    VectorPoint start;
    VectorPoint end;
};

struct VectorGlyph {
    std::vector<VectorStroke> strokes;
    float advance {};
};

const VectorGlyph* vector_glyph(char character);
float vector_text_width(std::string_view text);
constexpr float vector_text_height()
{
    return 1.0F;
}

} // namespace glide::flow
