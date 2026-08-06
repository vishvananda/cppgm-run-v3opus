# Per-tool implementation source lists for the compiler.
#
# Add dev/src/foo.cpp to the tools that use it by adding `foo` below. For
# subdirectories, use the path without `.cpp`, such as `parser/foo`.

FRONTEND_SOURCE_SET_TARGETS := abimangle pptoken posttoken ctrlexpr macro preproc recog nsdecl nsinit cy86 cppgm++ lowiropt lowir2cy86 lowir2native

FRONTEND_OBJ_BASENAMES_abimangle :=
FRONTEND_OBJ_BASENAMES_pptoken := source_charset source_reader pptoken_lexer
FRONTEND_OBJ_BASENAMES_posttoken := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer
FRONTEND_OBJ_BASENAMES_ctrlexpr := source_charset source_reader pptoken_lexer \
	token_model literal_scan ctrl_expr
FRONTEND_OBJ_BASENAMES_macro := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander
FRONTEND_OBJ_BASENAMES_preproc := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander ctrl_expr source_files preprocessor
FRONTEND_OBJ_BASENAMES_recog := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander ctrl_expr source_files preprocessor \
	parse_token memo_table recognizer recognizer_name recognizer_expression \
	recognizer_declarator recognizer_statement recognizer_member
FRONTEND_OBJ_BASENAMES_nsdecl := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander ctrl_expr source_files preprocessor \
	sema_token type_model entity_model program_model init_semantics \
	decl_parser decl_parser_declarator decl_parser_expression decl_parser_object
FRONTEND_OBJ_BASENAMES_nsinit := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander ctrl_expr source_files preprocessor \
	sema_token type_model entity_model program_model init_semantics \
	decl_parser decl_parser_declarator decl_parser_expression decl_parser_object
FRONTEND_OBJ_BASENAMES_cy86 := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander ctrl_expr source_files preprocessor \
	sema_token cy86_opcodes cy86_parser cy86_codegen x86_asm x86_elf
FRONTEND_OBJ_BASENAMES_cppgm++ := source_charset source_reader pptoken_lexer \
	token_model literal_scan string_literal posttokenizer \
	macro_model macro_expander ctrl_expr source_files preprocessor \
	ast_tokens ast_model ast_parser ast_parser_name ast_parser_class \
	ast_parser_declarator ast_parser_statement ast_parser_expression ast_emit
FRONTEND_OBJ_BASENAMES_lowiropt :=
FRONTEND_OBJ_BASENAMES_lowir2cy86 :=
FRONTEND_OBJ_BASENAMES_lowir2native :=
