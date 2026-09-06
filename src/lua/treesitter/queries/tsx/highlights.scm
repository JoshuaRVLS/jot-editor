[
  "break"
  "case"
  "catch"
  "continue"
  "debugger"
  "default"
  "do"
  "else"
  "finally"
  "for"
  "if"
  "return"
  "switch"
  "throw"
  "try"
  "while"
  "with"
] @keyword.control

[
  "abstract"
  "class"
  "const"
  "declare"
  "enum"
  "extends"
  "function"
  "implements"
  "interface"
  "let"
  "new"
  "private"
  "protected"
  "public"
  "readonly"
  "static"
  "type"
  "var"
] @keyword.storage

[
  "export"
  "import"
  "namespace"
] @keyword.directive

[
  "as"
  "delete"
  "in"
  "instanceof"
  "keyof"
  "of"
  "typeof"
  "void"
  "yield"
] @keyword
(this) @keyword

(string) @string
(template_string) @string
(comment) @comment
(number) @number
(regex) @string
(escape_sequence) @string.escape

(call_expression function: (identifier) @function)
(call_expression function: (member_expression property: (property_identifier) @function.method))
(function_declaration name: (identifier) @function)
(method_definition name: (property_identifier) @function.method)
(class_declaration name: (type_identifier) @type)
(interface_declaration name: (type_identifier) @type)
(type_alias_declaration name: (type_identifier) @type)
(enum_declaration name: (identifier) @type)
(type_identifier) @type
(predefined_type) @type.builtin
(required_parameter name: (identifier) @variable.parameter)
(optional_parameter name: (identifier) @variable.parameter)
(property_identifier) @property
(pair key: (property_identifier) @property)

; JSX / React: tags, attributes, text and expressions. Node shapes verified
; against the current tree-sitter-typescript grammar (jsx_element carries
; open_tag/close_tag fields; names are identifiers or member expressions).
(jsx_opening_element
  "<" @punctuation.bracket
  ">" @punctuation.bracket)
(jsx_opening_element name: (_) @tag)
(jsx_self_closing_element
  "<" @punctuation.bracket
  "/>" @punctuation.bracket)
(jsx_self_closing_element name: (_) @tag)
(jsx_closing_element
  "</" @punctuation.bracket
  ">" @punctuation.bracket)
(jsx_closing_element name: (_) @tag)
(jsx_attribute (property_identifier) @tag.attribute)
(jsx_attribute (jsx_namespace_name) @tag.attribute)
(jsx_text) @string
(jsx_expression
  "{" @punctuation.bracket
  "}" @punctuation.bracket)
(html_character_reference) @constant

(identifier) @variable