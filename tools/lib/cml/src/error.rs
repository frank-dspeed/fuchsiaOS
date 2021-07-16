// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_types::ParseError,
    std::path::Path,
    std::str::Utf8Error,
    std::{convert::TryFrom, error, fmt, io},
};

/// The location in the file where an error was detected.
#[derive(PartialEq, Clone, Debug)]
pub struct Location {
    /// One-based line number of the error.
    pub line: usize,

    /// One-based column number of the error.
    pub column: usize,
}

/// Enum type that can represent any error encountered by a cmx operation.
#[derive(Debug)]
pub enum Error {
    DuplicateRights(String),
    InvalidArgs(String),
    Io(io::Error),
    FidlEncoding(fidl::Error),
    MissingRights(String),
    Parse {
        err: String,
        location: Option<Location>,
        filename: Option<String>,
    },
    Validate {
        schema_name: Option<String>,
        err: String,
        filename: Option<String>,
    },
    Internal(String),
    Utf8(Utf8Error),
    /// A restricted feature was used without opting-in.
    RestrictedFeature(String),
}

impl error::Error for Error {}

impl Error {
    pub fn invalid_args(err: impl Into<String>) -> Self {
        Self::InvalidArgs(err.into())
    }

    pub fn parse(
        err: impl fmt::Display,
        location: Option<Location>,
        filename: Option<&Path>,
    ) -> Self {
        Self::Parse {
            err: err.to_string(),
            location,
            filename: filename.map(|f| f.to_string_lossy().into_owned()),
        }
    }

    pub fn validate(err: impl fmt::Display) -> Self {
        Self::Validate { schema_name: None, err: err.to_string(), filename: None }
    }

    pub fn validate_schema(schema_name: &str, err: impl Into<String>) -> Self {
        Self::Validate {
            schema_name: Some(schema_name.to_string()),
            err: err.into(),
            filename: None,
        }
    }

    pub fn duplicate_rights(err: impl Into<String>) -> Self {
        Self::DuplicateRights(err.into())
    }

    pub fn missing_rights(err: impl Into<String>) -> Self {
        Self::MissingRights(err.into())
    }

    pub fn internal(err: impl Into<String>) -> Self {
        Self::Internal(err.into())
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self {
            Error::DuplicateRights(err) => write!(f, "Duplicate rights: {}", err),
            Error::InvalidArgs(err) => write!(f, "Invalid args: {}", err),
            Error::Io(err) => write!(f, "IO error: {}", err),
            Error::FidlEncoding(err) => write!(f, "Fidl encoding error: {}", err),
            Error::MissingRights(err) => write!(f, "Missing rights: {}", err),
            Error::Parse { err, location, filename } => {
                let mut prefix = String::new();
                if let Some(filename) = filename {
                    prefix.push_str(&format!("{}:", filename));
                }
                if let Some(location) = location {
                    // Check for a syntax error generated by pest. These error messages have
                    // the line and column number embedded in them, so we don't want to
                    // duplicate that.
                    //
                    // TODO: If serde_json5 had an error type for json5 syntax errors, we wouldn't
                    // need to parse the string like this.
                    if !err.starts_with(" -->") {
                        prefix.push_str(&format!("{}:{}:", location.line, location.column));
                    }
                }
                if !prefix.is_empty() {
                    write!(f, "Error at {} {}", prefix, err)
                } else {
                    write!(f, "{}", err)
                }
            }
            Error::Validate { schema_name: _, err, filename } => {
                let mut prefix = String::new();
                if let Some(filename) = filename {
                    prefix.push_str(&format!("{}:", filename));
                }
                if !prefix.is_empty() {
                    write!(f, "Error at {} {}", prefix, err)
                } else {
                    write!(f, "{}", err)
                }
            }
            Error::Internal(err) => write!(f, "Internal error: {}", err),
            Error::Utf8(err) => write!(f, "UTF8 error: {}", err),
            Error::RestrictedFeature(feature) => write!(
                f,
                "Use of restricted feature \"{}\". To opt-in, see https://fuchsia.dev/fuchsia-src/development/components/build?hl=en#restricted-features",
                feature
            ),
        }
    }
}

impl From<io::Error> for Error {
    fn from(err: io::Error) -> Self {
        Error::Io(err)
    }
}

impl From<Utf8Error> for Error {
    fn from(err: Utf8Error) -> Self {
        Error::Utf8(err)
    }
}

impl From<serde_json::error::Error> for Error {
    fn from(err: serde_json::error::Error) -> Self {
        use serde_json::error::Category;
        match err.classify() {
            Category::Io | Category::Eof => Error::Io(err.into()),
            Category::Syntax => {
                let line = err.line();
                let column = err.column();
                Error::parse(err, Some(Location { line, column }), None)
            }
            Category::Data => Error::validate(err),
        }
    }
}

impl From<fidl::Error> for Error {
    fn from(err: fidl::Error) -> Self {
        Error::FidlEncoding(err)
    }
}

impl From<cm_json::Error> for Error {
    fn from(err: cm_json::Error) -> Self {
        match err {
            cm_json::Error::Io(err) => Error::Io(err),
            cm_json::Error::Parse { err, location, filename } => {
                Error::Parse { err, location: location.map(|loc| loc.into()), filename }
            }
            cm_json::Error::Validate { schema_name, err, filename } => {
                Error::Validate { schema_name, err, filename }
            }
        }
    }
}

impl From<ParseError> for Error {
    fn from(err: ParseError) -> Self {
        match err {
            ParseError::InvalidValue => Self::internal("invalid value"),
            ParseError::InvalidLength => Self::internal("invalid length"),
            ParseError::NotAName => Self::internal("not a name"),
            ParseError::NotAPath => Self::internal("not a path"),
        }
    }
}

impl From<cm_json::Location> for Location {
    fn from(location: cm_json::Location) -> Self {
        Location { line: location.line, column: location.column }
    }
}

impl TryFrom<serde_json5::Error> for Location {
    type Error = &'static str;
    fn try_from(e: serde_json5::Error) -> Result<Self, Self::Error> {
        match e {
            serde_json5::Error::Message { location: Some(l), .. } => {
                Ok(Location { line: l.line, column: l.column })
            }
            _ => Err("location unavailable"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cm_types as cm;
    use matches::assert_matches;

    #[test]
    fn test_syntax_error_message() {
        let result = serde_json::from_str::<cm::Name>("foo").map_err(Error::from);
        assert_matches!(result, Err(Error::Parse { .. }));
    }

    #[test]
    fn test_validation_error_message() {
        let result = serde_json::from_str::<cm::Name>("\"foo$\"").map_err(Error::from);
        assert_matches!(result, Err(Error::Validate { .. }));
    }
}
