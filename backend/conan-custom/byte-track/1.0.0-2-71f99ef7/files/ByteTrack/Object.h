#pragma once

#include "Rect.h"

namespace byte_track
{
    template<typename T>
    struct Object
    {
        Rect<float> rect_;
        int label_;
        float prob_;
        T data_;

        Object(const Rect<float> &rect,
               const int &label,
               const float &prob,
               T data
               ) : rect_(rect), label_(label), prob_(prob), data_(data) {

        }
    };
}