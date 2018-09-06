// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rand::{Rand, Rng};
use std::fmt;
use std::iter::Iterator;

const corner : char = '+';
const horiz  : char = '-';
const vert   : char = '|';
const pip    : char = '*';
const blank  : char = ' ';

#[derive(Debug)]
pub enum RollResult {
    One,
    Two,
    Three,
    Four,
    Five,
    Six,
}

impl Rand for RollResult {
    fn rand<R: Rng>(rng: &mut R) -> RollResult {
        match rng.gen_range(0, 6) {
            0 => RollResult::One,
            1 => RollResult::Two,
            2 => RollResult::Three,
            3 => RollResult::Four,
            4 => RollResult::Five,
            _ => RollResult::Six,
        }
    }
}

impl fmt::Display for RollResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let pips = match self {
            RollResult::One => [
                [blank, blank, blank],
                [blank, pip,   blank],
                [blank, blank, blank],
            ],
            RollResult::Two => [
                [blank, blank, pip],
                [blank, blank, blank],
                [pip,   blank, blank],
            ],
            RollResult::Three => [
                [blank, blank, pip],
                [blank, pip,   blank],
                [pip,   blank, blank],
            ],
            RollResult::Four => [
                [pip,   blank, pip],
                [blank, blank, blank],
                [pip,   blank, pip],
            ],
            RollResult::Five => [
                [pip,   blank, pip],
                [blank, pip,   blank],
                [pip,   blank, pip],
            ],
            RollResult::Six => [
                [pip,   blank, pip],
                [pip,   blank, pip],
                [pip,   blank, pip],
            ],
        };

        writeln!(f, "{}{}{}{}{}", corner, horiz, horiz, horiz, corner);
        for row in &pips {
            write!(f, "{}", vert);
            for c in row {
                write!(f, "{}", c);
            }
            writeln!(f, "{}", vert);
        }
        write!(f, "{}{}{}{}{}", corner, horiz, horiz, horiz, corner);

        Ok(())
    }
}

/// Iterator that yields N sized vectors of elements from N iterators.
///
/// All iterators must use the same item type. Iteration ends when the end of
/// any iterator is reached. If the number of iterators is known at compile
/// time, consider using itertools crate's izip! macro instead.
pub struct MultiZip<I : Iterator> {
    iters : Vec<I>,
}

pub fn multizip<I : Iterator>(iters: Vec<I>) -> MultiZip<I> {
    MultiZip {
        iters,
    }
}

impl<I : Iterator> Iterator for MultiZip<I> {
    type Item = Vec<I::Item>;

    fn next(&mut self) -> Option<Self::Item> {
        let mut pos = Vec::with_capacity(self.iters.len());
        for iter in self.iters.iter_mut() {
            if let Some(x) = iter.next() {
                pos.push(x);
            } else {
                return None;
            }
        }
        Some(pos)
    }
}

/// Run tests on a target device with "fx run-test rolldice_lib_test_rustc"
#[cfg(test)]
mod tests {
    use super::*;
    use std::iter::*;

    #[test]
    fn format_die_one() {
        let expected = "\
+---+
|   |
| * |
|   |
+---+";
        assert_eq!(format!("{}", RollResult::One), expected);
    }

    #[test]
    fn zip_empty_iterators() {
        let mut iter = multizip(vec![empty::<i32>(), empty(), empty()]);
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_single_element_iterators() {
        let mut iter = multizip(vec![once(1), once(2), once(3)]);
        assert_eq!(iter.next(), Some(vec![1, 2, 3]));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_equal_ranges() {
        let mut iter = multizip(vec![0..2, 2..4, 4..6, 6..8]);
        assert_eq!(iter.next(), Some(vec![0, 2, 4, 6]));
        assert_eq!(iter.next(), Some(vec![1, 3, 5, 7]));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_mismatched_ranges() {
        let mut iter = multizip(vec![0..3, 2..4, 4..6]);
        assert_eq!(iter.next(), Some(vec![0, 2, 4]));
        assert_eq!(iter.next(), Some(vec![1, 3, 5]));
        assert_eq!(iter.next(), None);

        let mut iter = multizip(vec![0..2, 2..5, 4..6]);
        assert_eq!(iter.next(), Some(vec![0, 2, 4]));
        assert_eq!(iter.next(), Some(vec![1, 3, 5]));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn zip_lines() {
        let strings = vec![
            String::from("A\nB\nC\n1"),
            String::from("D\nE\nF\n2"),
            String::from("G\nH\nI\n3"),
        ];
        let mut iter = multizip(strings.iter().map(|s| s.lines()).collect());
        assert_eq!(iter.next(), Some(vec!["A", "D", "G"]));
        assert_eq!(iter.next(), Some(vec!["B", "E", "H"]));
        assert_eq!(iter.next(), Some(vec!["C", "F", "I"]));
        assert_eq!(iter.next(), Some(vec!["1", "2", "3"]));
        assert_eq!(iter.next(), None);
    }
}
