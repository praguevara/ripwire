#pragma once

#include "model.h"
#include "resolve.h"   // resolveGleamModule — the import's own Step-A, so a call and its dependency edge cannot disagree

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace rw
{

// Gleam binds a reference by module and name, and ingest_gleam.h has already decided both in the reference's own file:
// `qualifier` is the imported module's path (empty: the caller's own module) and `calleeName` the name that module
// defines. Resolving is therefore two exact lookups: the file resolve.h's Gleam Step-A names for the module, then that
// file's definitions of the name. Nothing reaches the bare-name ladder, because a Gleam module has no global namespace:
// a same-spelled function in another module is never the target, and a miss is counted, never laddered (the Elixir
// resolver's rule, graph.h). A module no indexed file defines (`gleam/list`, a Hex package) is external.
struct GleamResolver
{
    // NoSuchName: an indexed module defines no such name. LocalField: neither, through a local named like the module —
    // the local's record field (ingest_gleam.h), which names no definition and is no miss.
    enum class Miss : std::uint8_t { None, ExternalModule, NoSuchName, LocalField };

    HashMap<std::string, std::vector<NodeId>> defs;         // "<fileId>#<name>" → the file's definitions of the name, ascending
    HashMap<std::string, std::uint32_t>       moduleFile;   // "<fileId>#<module>" → the file defining it, kNoFile if none

    static std::string keyOf( std::uint32_t fileId, std::string_view name )
    {
        std::string key = std::to_string( fileId );
        key += '#';
        key += name;
        return key;
    }

    // Every module a Gleam reference names is resolved here, once per (file, module), so the per-reference answer is a
    // lookup: the resolve loop only reads this table.
    GleamResolver( const IngestResult& ing, const WsIncludeCtx* ws )
    {
        bool any = false;
        for( const Symbol& symbol : ing.symbols )
        {
            if( symbol.lang == Lang::Gleam )
            {
                defs[ keyOf( symbol.fileId, symbol.name ) ].push_back( symbol.id );
                any = true;
            }
        }
        if( !any )
        {
            return;
        }
        const HashMap<std::string, std::uint32_t> fileIndex = normalizedFileIndex( ing );
        for( const Reference& ref : ing.references )
        {
            if( ref.lang != Lang::Gleam || ref.qualifier.empty() )
            {
                continue;
            }
            std::string key = keyOf( ref.fileId, ref.qualifier );
            if( !moduleFile.contains( key ) )
            {
                moduleFile.emplace( std::move( key ), resolveGleamModule( rootRelPath( ing, ref.fileId ), ref.qualifier, fileIndex, ws, ref.fileId ) );
            }
        }
        for( auto& [ key, nodes ] : defs )
        {
            std::sort( nodes.begin(), nodes.end() );
        }
    }

    // The definitions a Gleam `ref` names. A call never reaches a type: `Board(…)` constructs the constructor, not
    // `type Board`. Another language's reference is not this resolver's: `out` is left as it is and nothing is missed.
    Miss resolve( const IngestResult& ing, const Reference& ref, std::vector<NodeId>& out ) const
    {
        if( ref.lang != Lang::Gleam )
        {
            return Miss::None;
        }
        out.clear();
        std::uint32_t file = ref.fileId;
        if( !ref.qualifier.empty() )
        {
            const auto found = moduleFile.find( keyOf( ref.fileId, ref.qualifier ) );
            file = found == moduleFile.end() ? kNoFile : found->second;
            if( file == kNoFile )
            {
                return ref.recv == RecvKind::NamedVar ? Miss::LocalField : Miss::ExternalModule;   // a field, or an external call: no edge either way
            }
        }
        const auto found = defs.find( keyOf( file, ref.calleeName ) );
        if( found != defs.end() )
        {
            for( NodeId node : found->second )
            {
                const SymKind kind = ing.symbols[ node ].kind;
                if( ref.role != RefRole::Call || ( kind != SymKind::Struct && kind != SymKind::Alias ) )
                {
                    out.push_back( node );
                }
            }
        }
        if( !out.empty() )
        {
            return Miss::None;
        }
        return ref.recv == RecvKind::NamedVar ? Miss::LocalField : Miss::NoSuchName;
    }
};

}   // namespace rw
