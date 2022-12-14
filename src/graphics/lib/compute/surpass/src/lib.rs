// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// surpass does float comparison in a few places where the extra instructions generated by a
// full comparison would be too expensive in terms of computation.

mod affine_transform;
mod extend;
mod layer;
pub mod layout;
mod lines;
mod order;
pub mod painter;
mod path;
mod point;
pub mod rasterizer;
mod simd;
mod transform;

pub use affine_transform::AffineTransform;
pub use layer::Layer;
pub use lines::{GeomId, Lines, LinesBuilder};
pub use order::{Order, OrderError};
pub use path::{Path, PathBuilder};
pub use point::Point;
pub use transform::{GeomPresTransform, GeomPresTransformError};

const PIXEL_WIDTH: usize = 16;
const PIXEL_DOUBLE_WIDTH: usize = PIXEL_WIDTH * 2;
const PIXEL_SHIFT: usize = PIXEL_WIDTH.trailing_zeros() as usize;

pub const MAX_WIDTH: usize = 1 << 16;
pub const MAX_HEIGHT: usize = 1 << 15;

const MAX_WIDTH_SHIFT: usize = MAX_WIDTH.trailing_zeros() as usize;
const MAX_HEIGHT_SHIFT: usize = MAX_HEIGHT.trailing_zeros() as usize;

/// The width and height of the renderer's tiles.
pub const TILE_WIDTH: usize = 16;
const _ASSERT_TILE_WIFTH_MULTIPLE_OF_16: usize = 0 - (TILE_WIDTH % 16);
const _ASSERT_MAX_TILE_WIDTH: usize = 128 - TILE_WIDTH;
const TILE_WIDTH_SHIFT: usize = TILE_WIDTH.trailing_zeros() as usize;

pub const TILE_HEIGHT: usize = 16;
const _ASSERT_TILE_WIDTH_MULTIPLE_OF_16: usize = 0 - (TILE_HEIGHT % 16);
const _ASSERT_MAX_TILE_HEIGHT: usize = 128 - TILE_HEIGHT;
const TILE_HEIGHT_SHIFT: usize = TILE_HEIGHT.trailing_zeros() as usize;

const LAYER_LIMIT: usize = (1 << rasterizer::bit_field_lens::<TILE_WIDTH, TILE_HEIGHT>()[2]) - 1;

trait CanonBits {
    fn to_canon_bits(self) -> u32;
}

impl CanonBits for f32 {
    fn to_canon_bits(self) -> u32 {
        if self.is_nan() {
            return f32::NAN.to_bits();
        }

        if self == 0.0 {
            return 0.0f32.to_bits();
        }

        self.to_bits()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn f32_canon_bits_nan() {
        let nan0 = f32::NAN;
        let nan1 = nan0 + 1.0;

        assert_ne!(nan0, nan1);
        assert_eq!(nan0.to_canon_bits(), nan1.to_canon_bits());
    }

    #[test]
    fn f32_canon_bits_zero() {
        let neg_zero = -0.0f32;
        let pos_zero = 0.0;

        assert_eq!(neg_zero, pos_zero);
        assert_ne!(neg_zero.to_bits(), pos_zero.to_bits());
        assert_eq!(neg_zero.to_canon_bits(), pos_zero.to_canon_bits());
    }
}
