/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "HTMLParserIdioms.h"

#include "Decimal.h"
#include "URL.h"
#include <limits>
#include <wtf/MathExtras.h>
#include <wtf/text/AtomicString.h>
#include <wtf/text/StringBuilder.h>

namespace WebCore {

template <typename CharType>
static String stripLeadingAndTrailingHTMLSpaces(String string, CharType characters, unsigned length)
{
    unsigned numLeadingSpaces = 0;
    unsigned numTrailingSpaces = 0;

    for (; numLeadingSpaces < length; ++numLeadingSpaces) {
        if (isNotHTMLSpace(characters[numLeadingSpaces]))
            break;
    }

    if (numLeadingSpaces == length)
        return string.isNull() ? string : emptyAtom.string();

    for (; numTrailingSpaces < length; ++numTrailingSpaces) {
        if (isNotHTMLSpace(characters[length - numTrailingSpaces - 1]))
            break;
    }

    ASSERT(numLeadingSpaces + numTrailingSpaces < length);

    if (!(numLeadingSpaces | numTrailingSpaces))
        return string;

    return string.substring(numLeadingSpaces, length - (numLeadingSpaces + numTrailingSpaces));
}

String stripLeadingAndTrailingHTMLSpaces(const String& string)
{
    unsigned length = string.length();

    if (!length)
        return string.isNull() ? string : emptyAtom.string();

    if (string.is8Bit())
        return stripLeadingAndTrailingHTMLSpaces(string, string.characters8(), length);

    return stripLeadingAndTrailingHTMLSpaces(string, string.characters16(), length);
}

String serializeForNumberType(const Decimal& number)
{
    if (number.isZero()) {
        // Decimal::toString appends exponent, e.g. "0e-18"
        return number.isNegative() ? "-0" : "0";
    }
    return number.toString();
}

String serializeForNumberType(double number)
{
    // According to HTML5, "the best representation of the number n as a floating
    // point number" is a string produced by applying ToString() to n.
    return String::numberToStringECMAScript(number);
}

Decimal parseToDecimalForNumberType(const String& string, const Decimal& fallbackValue)
{
    // See HTML5 2.5.4.3 `Real numbers.' and parseToDoubleForNumberType

    // String::toDouble() accepts leading + and whitespace characters, which are not valid here.
    const UChar firstCharacter = string[0];
    if (firstCharacter != '-' && firstCharacter != '.' && !isASCIIDigit(firstCharacter))
        return fallbackValue;

    const Decimal value = Decimal::fromString(string);
    if (!value.isFinite())
        return fallbackValue;

    // Numbers are considered finite IEEE 754 single-precision floating point values.
    // See HTML5 2.5.4.3 `Real numbers.'
    // FIXME: We should use numeric_limits<double>::max for number input type.
    const Decimal floatMax = Decimal::fromDouble(std::numeric_limits<float>::max());
    if (value < -floatMax || value > floatMax)
        return fallbackValue;

    // We return +0 for -0 case.
    return value.isZero() ? Decimal(0) : value;
}

Decimal parseToDecimalForNumberType(const String& string)
{
    return parseToDecimalForNumberType(string, Decimal::nan());
}

double parseToDoubleForNumberType(const String& string, double fallbackValue)
{
    // See HTML5 2.5.4.3 `Real numbers.'

    // String::toDouble() accepts leading + and whitespace characters, which are not valid here.
    UChar firstCharacter = string[0];
    if (firstCharacter != '-' && firstCharacter != '.' && !isASCIIDigit(firstCharacter))
        return fallbackValue;

    bool valid = false;
    double value = string.toDouble(&valid);
    if (!valid)
        return fallbackValue;

    // NaN and infinity are considered valid by String::toDouble, but not valid here.
    if (!std::isfinite(value))
        return fallbackValue;

    // Numbers are considered finite IEEE 754 single-precision floating point values.
    // See HTML5 2.5.4.3 `Real numbers.'
    if (-std::numeric_limits<float>::max() > value || value > std::numeric_limits<float>::max())
        return fallbackValue;

    // The following expression converts -0 to +0.
    return value ? value : 0;
}

double parseToDoubleForNumberType(const String& string)
{
    return parseToDoubleForNumberType(string, std::numeric_limits<double>::quiet_NaN());
}

template <typename CharacterType>
static bool parseHTMLIntegerInternal(const CharacterType* position, const CharacterType* end, int& value)
{
    // Step 3
    int sign = 1;

    // Step 4
    while (position < end) {
        if (!isHTMLSpace(*position))
            break;
        ++position;
    }

    // Step 5
    if (position == end)
        return false;
    ASSERT_WITH_SECURITY_IMPLICATION(position < end);

    // Step 6
    if (*position == '-') {
        sign = -1;
        ++position;
    } else if (*position == '+')
        ++position;
    if (position == end)
        return false;
    ASSERT_WITH_SECURITY_IMPLICATION(position < end);

    // Step 7
    if (!isASCIIDigit(*position))
        return false;

    // Step 8
    StringBuilder digits;
    while (position < end) {
        if (!isASCIIDigit(*position))
            break;
        digits.append(*position++);
    }

    // Step 9
    bool ok;
    if (digits.is8Bit())
        value = sign * charactersToIntStrict(digits.characters8(), digits.length(), &ok);
    else
        value = sign * charactersToIntStrict(digits.characters16(), digits.length(), &ok);
    return ok;
}

// http://www.whatwg.org/specs/web-apps/current-work/#rules-for-parsing-integers
bool parseHTMLInteger(const String& input, int& value)
{
    // Step 1
    // Step 2
    unsigned length = input.length();
    if (!length || input.is8Bit()) {
        const LChar* start = input.characters8();
        return parseHTMLIntegerInternal(start, start + length, value);
    }

    const UChar* start = input.characters16();
    return parseHTMLIntegerInternal(start, start + length, value);
}

template <typename CharacterType>
static bool parseHTMLNonNegativeIntegerInternal(const CharacterType* position, const CharacterType* end, unsigned& value)
{
    // Step 3
    while (position < end) {
        if (!isHTMLSpace(*position))
            break;
        ++position;
    }

    // Step 4
    if (position == end)
        return false;
    ASSERT_WITH_SECURITY_IMPLICATION(position < end);

    // Step 5
    if (*position == '+')
        ++position;

    // Step 6
    if (position == end)
        return false;
    ASSERT_WITH_SECURITY_IMPLICATION(position < end);

    // Step 7
    if (!isASCIIDigit(*position))
        return false;

    // Step 8
    StringBuilder digits;
    while (position < end) {
        if (!isASCIIDigit(*position))
            break;
        digits.append(*position++);
    }

    // Step 9
    bool ok;
    if (digits.is8Bit())
        value = charactersToUIntStrict(digits.characters8(), digits.length(), &ok);
    else
        value = charactersToUIntStrict(digits.characters16(), digits.length(), &ok);
    return ok;
}


// http://www.whatwg.org/specs/web-apps/current-work/#rules-for-parsing-non-negative-integers
bool parseHTMLNonNegativeInteger(const String& input, unsigned& value)
{
    // Step 1
    // Step 2
    unsigned length = input.length();
    if (!length || input.is8Bit()) {
        const LChar* start = input.characters8();
        return parseHTMLNonNegativeIntegerInternal(start, start + length, value);
    }
    
    const UChar* start = input.characters16();
    return parseHTMLNonNegativeIntegerInternal(start, start + length, value);
}

static bool threadSafeEqual(const StringImpl& a, const StringImpl& b)
{
    if (&a == &b)
        return true;
    if (a.hash() != b.hash())
        return false;
    return equal(a, b);
}

bool threadSafeMatch(const QualifiedName& a, const QualifiedName& b)
{
    return threadSafeEqual(*a.localName().impl(), *b.localName().impl());
}

typedef Vector<ImageWithScale> ImageCandidates;

static inline bool compareByScaleFactor(const ImageWithScale& first, const ImageWithScale& second)
{
    return first.scaleFactor() < second.scaleFactor();
}

static bool parseDescriptors(const String& attribute, size_t start, size_t end, float& imageScaleFactor)
{
    size_t descriptorStart;
    size_t descriptorEnd;
    size_t position = start;
    bool isFoundScaleFactor = false;
    bool isEmptyDescriptor = !(end > start);
    bool isValid = false;

    while (position < end) {
        while (isHTMLSpace(attribute[position]) && position < end)
            ++position;
        descriptorStart = position;
        while (isNotHTMLSpace(attribute[position]) && position < end)
            ++position;
        descriptorEnd = position;

        // Leave if there is only whitespace at the end of the descriptors
        if (descriptorEnd <= descriptorStart)
            break;

        --descriptorEnd;
        // This part differs from the spec as the current implementation only supports pixel density descriptors.
        if (attribute[descriptorEnd] != 'x')
            continue;

        if (isFoundScaleFactor)
            return false;

        if (attribute.is8Bit())
            imageScaleFactor = charactersToFloat(attribute.characters8() + descriptorStart, descriptorEnd - descriptorStart, &isValid);
        else
            imageScaleFactor = charactersToFloat(attribute.characters16() + descriptorStart, descriptorEnd - descriptorStart, &isValid);
        isFoundScaleFactor = true;
    }

    return isEmptyDescriptor || isValid;
}

// See the specifications for more details about the algorithm to follow.
// http://www.w3.org/TR/2013/WD-html-srcset-20130228/#processing-the-image-candidates.
static void parseImagesWithScaleFromSrcsetAttribute(const String& srcsetAttribute, ImageCandidates& imageCandidates)
{
    ASSERT(imageCandidates.isEmpty());

    size_t imageCandidateStart = 0;
    unsigned srcsetAttributeLength = srcsetAttribute.length();

    while (imageCandidateStart < srcsetAttributeLength) {
        float imageScaleFactor = 1;
        size_t separator;

        // 4. Splitting loop: Skip whitespace.
        size_t imageURLStart = srcsetAttribute.find(isNotHTMLSpace, imageCandidateStart);
        if (imageURLStart == notFound)
            break;
        // If The current candidate is either totally empty or only contains space, skipping.
        if (srcsetAttribute[imageURLStart] == ',') {
            imageCandidateStart = imageURLStart + 1;
            continue;
        }
        // 5. Collect a sequence of characters that are not space characters, and let that be url.
        size_t imageURLEnd = srcsetAttribute.find(isHTMLSpace, imageURLStart + 1);
        if (imageURLEnd == notFound) {
            imageURLEnd = srcsetAttributeLength;
            separator = srcsetAttributeLength;
        } else if (srcsetAttribute[imageURLEnd - 1] == ',') {
            --imageURLEnd;
            separator = imageURLEnd;
        } else {
            // 7. Collect a sequence of characters that are not "," (U+002C) characters, and let that be descriptors.
            size_t imageScaleStart = srcsetAttribute.find(isNotHTMLSpace, imageURLEnd + 1);
            imageScaleStart = (imageScaleStart == notFound) ? srcsetAttributeLength : imageScaleStart;
            size_t imageScaleEnd = srcsetAttribute.find(',' , imageScaleStart + 1);
            imageScaleEnd = (imageScaleEnd == notFound) ? srcsetAttributeLength : imageScaleEnd;

            if (!parseDescriptors(srcsetAttribute, imageScaleStart, imageScaleEnd, imageScaleFactor)) {
                imageCandidateStart = imageScaleEnd + 1;
                continue;
            }
            separator = imageScaleEnd;
        }
        ImageWithScale image(imageURLStart, imageURLEnd - imageURLStart, imageScaleFactor);
        imageCandidates.append(image);
        // 11. Return to the step labeled splitting loop.
        imageCandidateStart = separator + 1;
    }
}

ImageWithScale bestFitSourceForImageAttributes(float deviceScaleFactor, const String& srcAttribute, const String& srcsetAttribute)
{
    ImageCandidates imageCandidates;

    parseImagesWithScaleFromSrcsetAttribute(srcsetAttribute, imageCandidates);

    if (!srcAttribute.isEmpty()) {
        ImageWithScale srcPlaceholderImage;
        imageCandidates.append(srcPlaceholderImage);
    }

    if (imageCandidates.isEmpty())
        return ImageWithScale();

    std::stable_sort(imageCandidates.begin(), imageCandidates.end(), compareByScaleFactor);

    for (size_t i = 0; i < imageCandidates.size() - 1; ++i) {
        if (imageCandidates[i].scaleFactor() >= deviceScaleFactor)
            return imageCandidates[i];
    }

    // 16. If an entry b in candidates has the same associated ... pixel density as an earlier entry a in candidates,
    // then remove entry b
    size_t winner = imageCandidates.size() - 1;
    size_t previousCandidate = winner;
    float winningScaleFactor = imageCandidates.last().scaleFactor();
    while ((previousCandidate > 0) && (imageCandidates[--previousCandidate].scaleFactor() == winningScaleFactor))
        winner = previousCandidate;

    return imageCandidates[winner];
}

}
