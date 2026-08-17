# Per-tool implementation source lists for the compiler.
#
# Add dev/src/foo.cpp to the tools that use it by adding `foo` below. For
# subdirectories, use the path without `.cpp`, such as `parser/foo`.

FRONTEND_SOURCE_SET_TARGETS := abimangle pptoken posttoken ctrlexpr macro preproc recog nsdecl nsinit cy86 cppgm++ lowiropt lowir2cy86 lowir2native

FRONTEND_OBJ_BASENAMES_abimangle := abi_mangle
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
	ast_parser_declarator ast_parser_statement ast_parser_expression ast_emit \
	type_model sema_name sema_scope sema_analyzer sema_using sema_class \
	sema_access \
	sema_lifetime \
	sema_declarator sema_function sema_layout sema_virtual sema_enum \
	sema_address sema_constant sema_constexpr sema_constexpr_declaration \
	sema_constexpr_object sema_constexpr_statement sema_noexcept \
	sema_statement sema_expression sema_cast sema_overload sema_init_list \
	sema_operator sema_argument_lookup \
	sema_deduce sema_derivation sema_pack sema_pattern sema_specialize sema_template \
	sema_template_head sema_template_signature sema_definition_names \
	sema_string_init \
	sema_type_id \
	sema_value_expression \
	sema_allocation \
	types_emit \
	semantics_emit abi_mangle lowir_abi lowir_text lowir_validate lowir_write \
	lowir_lower lowir_image lowir_lower_body lowir_lower_expression \
	lowir_lower_object lowir_lower_allocation lowir_lower_unwind \
	lowir_local_static \
	lowir_vtable lowir_emit
FRONTEND_OBJ_BASENAMES_lowiropt :=
FRONTEND_OBJ_BASENAMES_lowir2cy86 := lowir_text lowir_validate lowir_cy86
FRONTEND_OBJ_BASENAMES_lowir2native :=
