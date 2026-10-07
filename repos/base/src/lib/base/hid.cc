/*
 * \brief  Parser and generator for human-inclined data (HID)
 * \author Norman Feske
 * \date   2025-08-06
 */

/*
 * Copyright (C) 2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Genode includes */
#include <util/hid.h>

using namespace Genode;


namespace {

	struct Spaces
	{
		size_t const _n;

		void print(Output &out) const
		{
			for (unsigned i = 0; i < _n; i++) Genode::print(out, Char(' '));
		}
	};
}


Node::Match Node::_search(auto const &cond_fn) const
{
	char const *start     { };
	size_t      num_bytes { };
	Indent      indent    { };

	for_each_sub_node([&] (Node const &node) {
		if (!start && cond_fn(node)) {
			indent    = node._indent;
			start     = node._bytes.start;
			num_bytes = node._bytes.num_bytes;
		}
	});

	return { indent, { start, num_bytes } };
}


Node::Match Node::_search_by_type(char const *type) const
{
	return _search([&] (Node const &node) { return node.type() == type; });
}


Node::Match Node::_search_by_index(unsigned n) const
{
	unsigned i = 0;
	return _search([&] (Node const &) { return i++ == n; });
}


void Node::_for_each_sub_node(Span const &bytes, With_indent_span::Ft const &fn)
{
	struct Node
	{
		char const *start, *last;

		Indent indent;
		bool   enabled;

		size_t num_bytes()        const { return last - start + 1; };
		bool   contains(Indent i) const { return i.value > indent.value; }

	} node { nullptr, nullptr, { ~0U }, false };

	auto finish = [&]
	{
		if (node.start && node.last && node.enabled)
			fn(node.indent, Span(node.start, node.num_bytes()));
	};

	auto start = [&] (Indent indent, bool enabled, Span const &seg)
	{
		finish();
		node = { .start   = seg.start,
		         .last    = seg.start + seg.num_bytes - 1,
		         .indent  = indent,
		         .enabled = enabled };
	};

	auto extend = [&] (Span const &seg)
	{
		node.last = seg.start + seg.num_bytes - 1;
	};

	_for_each_segment(bytes,
		[&] (Prefix const  prefix, Indent const indent, Span const &seg) {
			if (prefix.node_or_xnode() && !node.contains(indent))
				start(indent, prefix.node(), seg);
			else
				extend(seg); });
	finish();
}


bool Node::has_sub_node(char const *type) const
{
	return with_sub_node(type, [&] (Node const &) { return true;  },
	                           [&]                { return false; });
}


void Node::_for_each_attr(Span const &bytes, auto const &fn)
{
	auto with_tag_value = [] (Span const &s, auto const &fn)
	{
		_with_ident(s, [&] (Span const &tag, Span const &remain) {
			if (tag.num_bytes && remain.num_bytes && remain.start[0] == ':')
				remain.cut(' ', [&] (Span const &, Span const &value) {
					_with_trimmed(value, [&] (Span const &trimmed_value) {
						fn(tag, trimmed_value); }); }); });
	};

	auto tag_exists = [] (Span const &seg)
	{
			bool result = false;
			_with_ident(seg, [&] (Span const &tag, Span const &remain) {
			if (tag.num_bytes && remain.num_bytes)
					result = remain.equals({ ":", 1 })
					      || remain.starts_with({ ": ", 2 }); });
			return result;
	};

	bool done = false;
	_for_each_segment(bytes,
		[&] (Prefix const prefix, Indent const, Span const &seg) {
			if (done)
				return;

			if (prefix.type == Prefix::TOP)
				seg.cut(' ', [&] (Span const &, Span const &seg) {
					_with_trimmed(seg, [&] (Span const &seg) {
						if (tag_exists(seg))
							with_tag_value(seg, fn);
						else if (seg.num_bytes)
							fn(Span("name", 4), seg);
					});
				});
			else if (prefix.type == Prefix::OTHER)
				_with_trimmed(seg, [&] (Span const &seg) {
					with_tag_value(seg, fn); });
			else
				done = true;
	});
}


void Node::_for_each_attribute(With_attribute::Ft const &fn) const
{
	_for_each_attr(_bytes, [&] (Span const &tag, Span const &value) {
		fn(Attribute { .tag   = { tag  .start, tag  .num_bytes },
		               .value = { value.start, value.num_bytes } }); });
}


bool Node::has_attribute(char const *type) const
{
	bool result = false;
	size_t const type_len = strlen(type);
	_for_each_attr(_bytes, [&] (Span const &tag, Span const &) {
		if (!result && tag.equals({ type, type_len })) result = true; });
	return result;
}


void Node::_with_tag_value(char const *type, With_tag_value::Ft const &fn) const
{
	size_t const type_len = strlen(type);

	bool found = false;
	_for_each_attr(_bytes, [&] (Span const &tag, Span const &value) {
		if (!found && tag.equals({ type, type_len })) {
			fn(tag, value);
			found = true; } });
}


static Span skipped_whitespace(Span const &bytes)
{
	char const *s = bytes.start;
	size_t      n = bytes.num_bytes;

	for (; n && is_whitespace(*s); n--, s++);

	return { s, n };
}


/**
 * Validate presence of node type and end marker for top-level node
 */
Const_byte_range_ptr Node::_validated(Const_byte_range_ptr const &bytes)
{
	bool valid = false;
	_with_type(bytes, [&] (Span const &t) { valid = (t.num_bytes > 0); });
	if (!valid)
		return { nullptr, 0 };

	/*
	 * Scan for end marker, reject node in the presence of control
	 * characters, except:
	 *
	 * - Tabs may appear within raw segments or comments
	 * - CR is followed by LF as part of a line ending
	 */
	char const control_mask = ~0x1f;
	char next = bytes.start[0];
	enum { START, ACCEPT, REJECT } tabs { };
	for (unsigned n = 1; n < bytes.num_bytes; n++) {
		char const curr = next;
		next = bytes.start[n];

		if (tabs == START && (curr == ':' || curr == '.')) tabs = ACCEPT;
		if (tabs == START && (curr != ' '))                tabs = REJECT;
		if (curr == '|' || curr == '\n')                   tabs = START;

		if (!(curr & control_mask)) {
			if (curr == '\n' && _minus(next)) /* end marker */
				return { bytes.start, n + 1 };

			if (curr == '\n')                   continue;
			if (curr == '\r' && next == '\n')   continue;
			if (curr == '\t' && tabs == ACCEPT) continue;
			break;
		}
	}
	return { nullptr, 0 };
}


void Node::Quoted_content::print(Output &out) const
{
	_node.for_each_quoted_line([&] (auto const &line) {
		line.print(out);
		if (!line.last) out.out_char('\n'); });
}


Node::Node(Span const &bytes) : _bytes(_validated(skipped_whitespace(bytes))) { }


/***************
 ** Generator **
 ***************/

/**
 * Meta data for the formatted output of 'Generator::tabular'
 */
struct Generator::Tabular : Noncopyable
{
	Generator &_g;

	/*
	 * The functor argument of 'tabular()' is evaluated twice. The first
	 * phase collects the node and attribute structure along with their
	 * sizes. The second phases uses the collected 'layout' data to print
	 * the output.
	 */
	enum Phase { GATHER_LAYOUT, PRINT } phase { GATHER_LAYOUT };

	static constexpr unsigned MAX_LEVELS = 4; /* maximum nesting level */
	static constexpr unsigned MAX_ATTR   = 8; /* max attributes per node */

	struct Node_type { size_t len; };

	/*
	 * Data structure used for recording size values of one row
	 */
	struct Gathered_row
	{
		struct Node
		{
			/*
			 * Record 'tag_and_value' rather than the 'tag' size to cover the
			 * special case of the "name" as first attribute, which needs no tag.
			 */
			struct Attr { size_t tag, tag_and_value, value; } attr[MAX_ATTR] { };

			unsigned  num_attr { };  /* recorded attributes for this node */
			Node_type type     { };  /* length of node type */

			size_t needed_column_width() const
			{
				size_t w = 2 /* plus, space */ + type.len + 1 /* space */ ;
				for (unsigned i = 0; i < num_attr; i++)
					w += attr[i].tag_and_value;

				return w + 1;
			}

			void attach_attr(Attr a)
			{
				if (num_attr < MAX_ATTR) { attr[num_attr] = a; num_attr++; }
			}

		} nodes[MAX_LEVELS] { };

		unsigned level = 0; /* nesting level */

		void attach_node_to_row(Node_type type)
		{
			if (level < MAX_LEVELS - 1) {
				level++;
				nodes[level].num_attr = 0;
				nodes[level].type     = type;
			}
		}

		void attach_attr(Node::Attr as) { nodes[level].attach_attr(as); }
	};

	/**
	 * Layout that gets successively refined by applying all gathered rows
	 */
	struct Layout
	{
		struct Node
		{
			struct Attr
			{
				size_t max_tag, max_tag_and_value, max_value;
				bool   tags_contradict;

				void update(Gathered_row::Node::Attr a)
				{
					max_tag           = max(max_tag,           a.tag);
					max_tag_and_value = max(max_tag_and_value, a.tag_and_value);
					max_value         = max(max_value,         a.value);
					if (max_tag != a.tag)
						tags_contradict = true;
				}

			} attr[MAX_ATTR] { };

			unsigned num_attr = 0;

			Node_type max_type { };         /* used to detect 'types_contradict' */

			bool types_contradict = false;  /* type names have different lengths */

			size_t max_packed_width = 0;

			void update(Gathered_row::Node const &node)
			{
				max_packed_width = max(max_packed_width, node.needed_column_width());
				num_attr         = max(num_attr, node.num_attr);
				max_type.len     = max(max_type.len, node.type.len);

				if (max_type.len != node.type.len)
					types_contradict = true;

				for (unsigned i = 0; i < node.num_attr; i++)
					attr[i].update(node.attr[i]);
			}

		} nodes[MAX_LEVELS] { };

		unsigned level = 0;

		void update(Gathered_row const &row)
		{
			for (unsigned i = 0; i <= row.level; i++)
				nodes[i].update(row.nodes[i]);

			level = max(level, row.level);
		}
	};

	Gathered_row curr { };

	Layout layout { };

	struct Print_pos { unsigned level, attr; size_t anchor_out_offset; };

	Print_pos print_pos { };

	Indent const anchor_indent { _g._node_state.indent };

	size_t const _leading_anchor_spaces = max(2*anchor_indent.level, 2u) - 2;

	Tabular(Generator &g) : _g(g) { _g._tabular_ptr = this; }

	~Tabular() { /* C++ exception during 'fn' */ _g._tabular_ptr = nullptr; }

	void new_row(Node_type type)
	{
		layout.update(curr);
		curr = { };
		curr.nodes[0].type = type;
	}

	size_t _printed_node_hpos() const
	{
		size_t pos = _leading_anchor_spaces;
		for (unsigned i = 0; i <= print_pos.level; i++) {
			if (i > 0) pos += 2; /* sparator between nodes (pipe, space) */
			pos += layout.nodes[i].max_packed_width;
		}
		return pos;
	}

	size_t leading_spaces_before_node(Out_buffer const &out_buffer) const
	{
		size_t const limit = _printed_node_hpos();
		size_t const used  = out_buffer.used() - print_pos.anchor_out_offset;

		return limit > used ? limit - used : 1ul; /* enforce at least 1 space */
	}

	size_t leading_spaces_before_sibling_node() const
	{
		size_t const limit = _printed_node_hpos();

		return limit > 0 ? limit - 1 : 0;
	}

	size_t leading_spaces_before_attr(Out_buffer const &out_buffer) const
	{
		/* do not align attributes within columns of (nested) nodes */
		if (layout.level > 0)
			return 0;

		Layout::Node const &node = layout.nodes[0];
		if (node.types_contradict)
			return 0;

		size_t pos = _leading_anchor_spaces + 2 /* plus, space */ + node.max_type.len;

		if (node.num_attr > 0) pos += 1; /* space after node type */

		for (unsigned i = 0; i < print_pos.attr; i++) {
			Layout::Node::Attr const &attr = node.attr[i];

			/* don't try to align attributes with different tag lengths */
			if (node.attr[i].tags_contradict)
				return 0;

			size_t const max_tag_value = attr.max_tag + 2 /* colon, space */
			                           + attr.max_value;

			bool const name = (i == 0) && (attr.max_tag == 0);

			pos += name ? attr.max_value
			            : 2 /* pipe, space */ + max_tag_value;

			pos++; /* space */
		}

		size_t const used = out_buffer.used() - print_pos.anchor_out_offset;

		return pos > used ? pos - used : 0ul;
	}
};


void Generator::_attribute(char const *tag, char const *value, size_t val_len)
{
	/* deny non-printable and delimiting characters in attribute values */
	auto blessed = [] (char c) { return (c & 0xe0) && (c != '|'); };
	for (size_t i = 0; i < val_len; i++) {
		if (blessed(value[i]))
			continue;

		static bool warned_once;
		if (!warned_once)
			warning("attribute '", tag, "' contains invalid character ", value[i]);
		warned_once = true;
		return;
	}

	auto value_without_tag_chars = [&]
	{
		for (size_t i = 0; i < val_len; i++)
			if ((value[i] == ':') && (i + 1 == val_len || value[i + 1] == ' '))
				return false;
		return true;
	};

	size_t const tag_len = strlen(tag);

	bool const first         = !_node_state.has_attr;
	bool const name_as_first = first && (strcmp(tag, "name") == 0)
	                                 && value_without_tag_chars();

	size_t leading_spaces = 0;

	if (_tabular_ptr) {

		Tabular &tabular = *_tabular_ptr;

		if (tabular.phase == Tabular::Phase::GATHER_LAYOUT) {

			size_t const sep = first ? 1 /* space */ : 3; /* space, pipe, space */
			size_t const tag_and_colon = name_as_first
			                           ? 0 : tag_len + 2; /* colon, space */

			tabular.curr.attach_attr({
				.tag           = name_as_first ? 0ul : tag_len,
				.tag_and_value = sep + tag_and_colon + val_len,
				.value         = val_len
			});
			_node_state.has_attr = true;
			return;
		}

		leading_spaces = tabular.leading_spaces_before_attr(_out_buffer);

		if (tabular.print_pos.attr < Tabular::MAX_ATTR - 1)
			tabular.print_pos.attr++;
	}

	auto insert = [&] (size_t gap, auto const &fn)
	{
		_out_buffer.with_inserted_gap(_node_state.attr_offset, gap, [&] (Out_buffer &out) {
			fn(out);
			_node_state.attr_offset += gap; });
	};

	if (name_as_first)
		insert(1 + val_len, [&] (Out_buffer &out) {
			print(out, " ", Cstring(value, val_len)); });
	else
		insert(leading_spaces + 3 + tag_len + 2 + val_len, [&] (Out_buffer &out) {
			print(out, Spaces(leading_spaces), " | ", tag, ": ", Cstring(value, val_len)); });

	_node_state.has_attr = true;
}


void Generator::_print_node_type(Span const &name)
{
	Cstring const name_str { name.start, name.num_bytes };
	if (_node_state.indent.level == 0) {
		print(_out_buffer, name_str);
		return;
	}

	auto print_sub_node_at_new_line = [&]
	{
		print(_out_buffer, "\n", _node_state.indent, "+ ", name_str);
	};

	if (!_tabular_ptr) {
		print_sub_node_at_new_line();
		return;
	}

	/*
	 * Gather tabular layout
	 */
	Tabular &tabular = *_tabular_ptr;

	if (tabular.phase == Tabular::Phase::GATHER_LAYOUT) {
		Tabular::Node_type const node_type { name.num_bytes };
		if (_node_state.indent.level == tabular.anchor_indent.level)
			tabular.new_row(node_type);
		else
			tabular.curr.attach_node_to_row(node_type);
		return;
	}

	/* print type of first node of a new row */
	if (_node_state.indent.level == tabular.anchor_indent.level) {
		/* new table row */
		tabular.print_pos = { .level = 0, .attr = 0,
		                      .anchor_out_offset = _out_buffer.used() };
		print_sub_node_at_new_line();
		return;
	}

	/* print type of sub node aligned at table column */
	print(_out_buffer, Spaces(tabular.leading_spaces_before_node(_out_buffer)),
	                   "| + ", name_str);

	if (tabular.print_pos.level < Tabular::MAX_LEVELS - 1)
		tabular.print_pos.level++;

	tabular.print_pos.attr = 0;
}


void Generator::_node(char const *name, Node_fn::Ft const &fn)
{
	_print_node_type({ name, strlen(name) });

	if (_out_buffer.exceeded())
		return;

	{
		struct Orig { size_t used; Node_state node_state; };
		Orig const orig { _out_buffer.used(), _node_state, };

		_node_state = { .indent      = { _node_state.indent.level + 1 },
		                .attr_offset = _out_buffer.used(),
		                .has_attr    = false,
		                .quote       = { ":", false, false },
		                .doc         = { ".", false, false }, };

		struct Guard
		{
			Generator &g; Orig orig; bool ok;

			~Guard()
			{
				g._node_state = orig.node_state;
				if (!ok)
					g._out_buffer.rewind(orig.used);
			}
		} guard { .g = *this, .orig = orig, .ok = false };

		fn();
		guard.ok = true;
	}

	if (_node_state.indent.level == 0)
		print(_out_buffer, "\n-\n");
}


void Generator::_tabular(Node_fn::Ft const &fn)
{
	/* squash nested tabular scopes into one */
	if (_tabular_ptr) {
		fn();
		return;
	}

	Tabular tabular { *this };

	/* gather layout info */
	fn();
	tabular.new_row({ }); /* collect layout info of last node */

	/* print */
	tabular.phase = Tabular::Phase::PRINT;
	fn();

	_tabular_ptr = nullptr;
}


void Generator::_copy(Node const &node)
{
	if (_tabular_ptr && _tabular_ptr->phase == Tabular::Phase::GATHER_LAYOUT)
		return;

	auto with_stripped_indentation = [&] (Span const &line, auto const &fn)
	{
		size_t const skip = node._indent.value;
		if (line.num_bytes >= skip)
			fn(Span { line.start + skip, line.num_bytes - skip });
		else
			fn(Span { nullptr, 0 });
	};

	bool first = true;
	node._bytes.split('\n', [&] (Span const &line) {
		if (line.starts_with({ "-", 1 })) /* exclude end marker of top-level node */
			return;

		print(_out_buffer, "\n", _node_state.indent);
		if (first)
			print(_out_buffer, "+ ", Cstring(line.start, line.num_bytes));
		else
			with_stripped_indentation(line, [&] (Span const &line) {
				print(_out_buffer, "  ", Cstring(line.start, line.num_bytes)); });

		first = false;
	});
}


void Generator::_start_prefixed_line(Node_state::Prefixed &prefixed)
{
	if (_tabular_ptr) {
		bool const any_prefix_started = _node_state.quote.started
		                              | _node_state.doc.started;
		Tabular const &tabular = *_tabular_ptr;
		if (any_prefix_started) {
			Spaces align { tabular.leading_spaces_before_sibling_node() };
			print(_out_buffer, "\n", align, "| ");
		} else /* attach first line to preceeding node */ {
			Spaces align { tabular.leading_spaces_before_node(_out_buffer) };
			print(_out_buffer, align, "| ");
		}
	} else {
		print(_out_buffer, "\n", _node_state.indent);
	}
	print(_out_buffer, prefixed.prefix); /* omit trailing space for empty line */

	prefixed.started   = true;
	prefixed.line_used = false;
}


void Generator::_append_prefixed(Span const &s, Node_state::Prefixed &prefixed)
{
	/* suppress printing in table-layout gathering phase */
	if (_tabular_ptr && _tabular_ptr->phase == Tabular::Phase::GATHER_LAYOUT)
		return;

	if (!prefixed.started) _start_prefixed_line(prefixed);

	bool first = true;
	s.split('\n', [&] (Span const &fragment) {

		/* subsequent fragments are always preceeded by a newline */
		if (!first) _start_prefixed_line(prefixed);

		/* append content to current line */
		if (fragment.num_bytes) {
			if (!prefixed.line_used) {
				print(_out_buffer, " ");
				prefixed.line_used = true;
			}
			print(_out_buffer, Cstring { fragment.start, fragment.num_bytes });
		}
		first = false;
	});
}


void Generator::_append_quoted(Span const &s)
{
	_append_prefixed(s, _node_state.quote);
}


void Generator::_append_doc(Span const &s)
{
	_append_prefixed(s, _node_state.doc);
}
