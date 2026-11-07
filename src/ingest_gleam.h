#pragma once

#if !defined( RIPWIRE_INGEST_TU )
#error "ingest_gleam.h is a section of ingest.cpp; include it only there"
#endif

// Gleam extraction: the per-file name table every Gleam reference is decided against.
//
// A Gleam module's names are closed and lexical. It defines its own top-level functions, constants and constructors;
// `import a/b.{f, g as h, type T} as c` brings in the module `a/b` under the local name `c` (by default its last path
// segment, `b`) and the unqualified names in braces; and nothing else is in scope: no glob import, no global namespace,
// no nested named function. So a reference is decided inside its own file, at capture time, with no guessing:
//   * `c.f` (a call, a pipe target, a function value) names `f` of module `a/b` when `c` is an imported module. Any other
//     `x.f` is a record field access, which names no definition, and emits nothing;
//   * a bare `f` names the module's own `f`, or the unqualified import `import m.{g as f}` (calleeName g, qualifier m).
//     Any other bare name is a local (a parameter, a `let`, a `use` binding, a closure), and emits nothing;
//   * a constructor `Ok(x)` / `c.Leaf(x)` follows the same two rules;
//   * a local SHADOWS a bare name: a parameter, a `let`/`use` binding or a pattern named `apply` makes `apply(x)` a
//     closure call everywhere in the top-level definition that binds it (Gleam scopes a local from its binding on, so a
//     use BEFORE a same-named `let` is read as the local: the edge is missed, never invented);
//   * a local named like an imported module does NOT hide the module: Gleam reads `lasting.drained` as the local's
//     field and `lasting.standing(…)` as the module's function, trying the record first. Without types the reference is
//     emitted with the local as its receiver (RecvKind::NamedVar), and gleam_resolve.h binds it only when the module
//     defines the name; otherwise it is the field, silently. A record whose field and its module's function share a name
//     reads as the function: a disclosed floor.
// The qualifier is the imported module's PATH (`palimpsest_core/fight/board`); gleam_resolve.h answers it with the file
// that defines the module (`…/src/palimpsest_core/fight/board.gleam`), and an empty qualifier with the caller's own file.

namespace rw
{
namespace
{

struct GleamNames
{
    struct Imported
    {
        std::string_view module;   // the imported module's path, as written
        std::string_view name;     // the name it defines (`g` for `g as f`)
    };
    HashMap<std::string_view, std::string_view> modules;       // local module name → module path
    HashMap<std::string_view, Imported>         unqualified;   // local unqualified name → its module and own name
    HashMap<std::string_view, char>             own;           // the top-level names this module defines
    HashMap<std::uint32_t, std::uint32_t>       attributeRun;  // a top-level definition's start byte → its attribute run's start
    struct Locals
    {
        std::uint32_t                   start = 0;
        std::uint32_t                   end   = 0;
        HashMap<std::string_view, char> bound;   // every name a pattern of the definition binds
    };
    std::vector<Locals> locals;   // one row per top-level function, in source order (byte-sorted, disjoint)

    // One pass over the file root's direct children: definitions are all top level, and so is every import.
    void prepare( TSNode root, std::string_view src )
    {
        const std::uint32_t count = ts_node_named_child_count( root );
        std::uint32_t runStart = 0;
        bool          inRun    = false;
        for( std::uint32_t i = 0; i < count; ++i )
        {
            const TSNode      child = ts_node_named_child( root, i );
            const char* const kind  = ts_node_type( child );
            if( kindIs( kind, "attribute" ) )
            {
                runStart = inRun ? runStart : ts_node_start_byte( child );
                inRun    = true;
                continue;
            }
            if( inRun )
            {
                attributeRun[ ts_node_start_byte( child ) ] = runStart;
                inRun = false;
            }
            if( kindIs( kind, "import" ) )
            {
                addImport( child, src );
            }
            else if( kindIs( kind, "function" ) || kindIs( kind, "external_function" ) || kindIs( kind, "constant" ) )
            {
                own[ nodeTextOf( fieldChild( child, NodeField::Name ), src ) ] = 1;
                if( kindIs( kind, "function" ) )
                {
                    locals.push_back( { ts_node_start_byte( child ), ts_node_end_byte( child ), boundNames( child, src ) } );
                }
            }
            else if( kindIs( kind, "type_definition" ) )
            {
                addConstructors( child, src );
            }
        }
    }

    void addImport( TSNode import, std::string_view src )
    {
        const std::string_view module = nodeTextOf( fieldChild( import, NodeField::Module ), src );
        if( module.empty() )
        {
            return;
        }
        const TSNode           alias = fieldChild( import, NodeField::Alias );
        const std::size_t      slash = module.rfind( '/' );
        const std::string_view local = !ts_node_is_null( alias ) ? nodeTextOf( alias, src )
                                     : slash == std::string_view::npos ? module : module.substr( slash + 1 );
        if( !local.empty() && local.front() != '_' )   // `as _` discards the module name, keeping only the braces
        {
            modules[ local ] = module;
        }
        const TSNode names = fieldChild( import, NodeField::Imports );
        const std::uint32_t count = ts_node_is_null( names ) ? 0 : ts_node_named_child_count( names );
        for( std::uint32_t i = 0; i < count; ++i )
        {
            const TSNode name = ts_node_named_child( names, i );
            const TSNode nameNode = fieldChild( name, NodeField::Name );
            if( kindIs( ts_node_type( nameNode ), "type_identifier" ) )
            {
                continue;   // `type T`: a type, never a call target
            }
            const TSNode           renamed = fieldChild( name, NodeField::Alias );
            const std::string_view written = nodeTextOf( nameNode, src );
            unqualified[ ts_node_is_null( renamed ) ? written : nodeTextOf( renamed, src ) ] = { module, written };
        }
    }

    void addConstructors( TSNode type, std::string_view src )
    {
        const std::uint32_t count = ts_node_named_child_count( type );
        for( std::uint32_t i = 0; i < count; ++i )
        {
            const TSNode list = ts_node_named_child( type, i );
            if( !kindIs( ts_node_type( list ), "data_constructors" ) )
            {
                continue;
            }
            const std::uint32_t ctorCount = ts_node_named_child_count( list );
            for( std::uint32_t c = 0; c < ctorCount; ++c )
            {
                own[ nodeTextOf( fieldChild( ts_node_named_child( list, c ), NodeField::Name ), src ) ] = 1;
            }
        }
    }

    // Every name a pattern inside `fn` binds: parameters (its own and its closures'), `let`/`let assert` patterns, `use`
    // assignments and case-clause patterns. One cursor walk; `inPattern` is carried down from the binding node, so an
    // identifier in a pattern is a binding and one anywhere else (a `let`'s value, a guard, a body) is a use.
    static HashMap<std::string_view, char> boundNames( TSNode fn, std::string_view src )
    {
        HashMap<std::string_view, char> bound;
        const TSFieldId patternField = fieldIdFor( ts_node_language( fn ), NodeField::Pattern );
        TSTreeCursor    cursor       = ts_tree_cursor_new( fn );
        struct Frame { TSNode node; const char* kind; bool inPattern; };   // the parent of the cursor's current node, per depth
        std::vector<Frame> frames { { fn, "", false } };
        for( ;; )
        {
            const TSNode      node    = ts_tree_cursor_current_node( &cursor );
            const char* const kind    = ts_node_type( node );
            const Frame&      parent  = frames.back();
            const bool        pattern = parent.inPattern || kindIs( kind, "function_parameter" ) || kindIs( kind, "use_assignment" )
                                     || kindIs( kind, "case_clause_patterns" )
                                     || ( ( kindIs( parent.kind, "let" ) || kindIs( parent.kind, "let_assert" ) )
                                          && ts_tree_cursor_current_field_id( &cursor ) == patternField );
            // `Person(lasting:, ..)` binds `lasting` through the label alone: a label shorthand has no pattern of its own
            const bool shorthand = pattern && kindIs( kind, "label" ) && kindIs( parent.kind, "record_pattern_argument" )
                                && ts_node_is_null( fieldChild( parent.node, NodeField::Pattern ) );
            if( pattern && ( kindIs( kind, "identifier" ) || shorthand ) )
            {
                bound[ nodeTextOf( node, src ) ] = 1;
            }
            if( ts_tree_cursor_goto_first_child( &cursor ) )
            {
                frames.push_back( { node, kind, pattern } );
                continue;
            }
            while( !ts_tree_cursor_goto_next_sibling( &cursor ) )
            {
                if( frames.size() == 1 || !ts_tree_cursor_goto_parent( &cursor ) )
                {
                    ts_tree_cursor_delete( &cursor );
                    return bound;
                }
                frames.pop_back();
            }
        }
    }

    // Whether `name` is a local at byte `at`: bound by a pattern of the top-level function holding `at`.
    bool isLocal( std::string_view name, std::uint32_t at ) const
    {
        const auto row = std::upper_bound( locals.begin(), locals.end(), at, []( std::uint32_t b, const Locals& l ) { return b < l.start; } );
        if( row == locals.begin() )
        {
            return false;
        }
        const Locals& l = *std::prev( row );
        return at < l.end && l.bound.contains( name );
    }

    // The start of the attribute run above a top-level definition (`@external(…)`), or the definition's own start.
    std::uint32_t startWithAttributes( TSNode defNode ) const
    {
        const std::uint32_t start = ts_node_start_byte( defNode );
        const auto          found = attributeRun.find( start );
        return found == attributeRun.end() ? start : found->second;
    }

    // Decide one captured reference (see the header): fill its name and qualifier, or return false to emit nothing.
    bool qualify( RawRef& r, TSNode nameNode, std::string_view src ) const
    {
        const TSNode           parent  = ts_node_parent( nameNode );
        const char* const      outer   = ts_node_type( parent );
        const bool             access  = kindIs( outer, "field_access" ) || kindIs( outer, "remote_constructor_name" );
        const std::string_view written = nodeTextOf( nameNode, src );
        if( access )
        {
            const TSNode record = fieldChild( parent, kindIs( outer, "field_access" ) ? NodeField::Record : NodeField::Module );
            if( !kindIs( ts_node_type( record ), "identifier" ) )
            {
                return false;
            }
            const std::string_view recordName = nodeTextOf( record, src );
            const auto             module     = modules.find( recordName );
            if( module == modules.end() )
            {
                return false;   // a record field, or a module this file does not import
            }
            r.name.assign( written );
            r.qualifier.assign( module->second );
            if( isLocal( recordName, ts_node_start_byte( record ) ) )
            {
                r.recv = RecvKind::NamedVar;   // the local's field unless the module defines the name (gleam_resolve.h)
                r.recvVar.assign( recordName );
            }
            return true;
        }
        if( isLocal( written, ts_node_start_byte( nameNode ) ) )
        {
            return false;   // a parameter, `let` or pattern binding: a closure call or a value, never a definition
        }
        if( const auto imported = unqualified.find( written ); imported != unqualified.end() )
        {
            r.name.assign( imported->second.name );
            r.qualifier.assign( imported->second.module );
            return true;
        }
        if( own.contains( written ) )
        {
            r.name.assign( written );
            return true;
        }
        return false;
    }
};

}   // namespace
}   // namespace rw
