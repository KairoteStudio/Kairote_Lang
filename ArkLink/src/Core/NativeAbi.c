#include "ArkLink/NativeAbi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AbiRecord {
    const uint8_t* nominal;
    const uint8_t* contract;
    uint32_t nominal_size;
    uint32_t contract_size;
    uint64_t hash;
    int defined;
    int kind;
    int weak;
    const char* path;
    struct AbiRecord* next;
} AbiRecord;

typedef struct AbiIndex {
    AbiRecord** buckets;
    size_t capacity;
    size_t count;
} AbiIndex;

static uint32_t word(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t hash_bytes(const uint8_t* p, size_t size) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}
static int prefix(const char* name, const char* value) {
    return name && strncmp(name, value, strlen(value)) == 0;
}
static int range(size_t offset, size_t size, size_t total) {
    return offset <= total && size <= total - offset;
}
static ArkLinkResult failure(const char* code, const char* path, const char* detail) {
    fprintf(stderr, "%s: %s: %s\n", path ? path : "<object>", code, detail);
    return ARK_LINK_ERR_FORMAT;
}
static void index_destroy(AbiIndex* index) {
    if (!index->buckets) return;
    for (size_t i = 0; i < index->capacity; i++) {
        AbiRecord* item = index->buckets[i];
        while (item) { AbiRecord* next = item->next; free(item); item = next; }
    }
    free(index->buckets);
}
static ArkLinkResult insert(AbiIndex* index, const AbiRecord* record) {
    size_t bucket = (size_t)(record->hash & (index->capacity - 1));
    for (AbiRecord* item = index->buckets[bucket]; item; item = item->next) {
        if (item->hash != record->hash || item->nominal_size != record->nominal_size ||
            memcmp(item->nominal, record->nominal, record->nominal_size)) continue;
        if (item->kind != record->kind || item->contract_size != record->contract_size ||
            memcmp(item->contract, record->contract, record->contract_size))
            return failure("E_ABI_LAYOUT", record->path, "exact native contract conflicts with another object");
        if (record->kind != 2 && item->defined && record->defined && !item->weak && !record->weak)
            return failure("E_ABI_SYMBOL", record->path, "duplicate strong native definition");
        if (record->defined && (!item->defined || item->weak)) {
            item->defined = 1; item->weak = record->weak;
        }
        return ARK_LINK_OK;
    }
    if (index->count >= 1048576) return failure("E_ABI_MANIFEST", record->path, "too many native contract identities");
    AbiRecord* item = malloc(sizeof(*item));
    if (!item) return ARK_LINK_ERR_MEMORY;
    *item = *record; item->next = index->buckets[bucket]; index->buckets[bucket] = item; index->count++;
    if (index->count < index->capacity * 3 / 4 || index->capacity >= 1048576) return ARK_LINK_OK;
    size_t capacity = index->capacity * 2;
    AbiRecord** buckets = calloc(capacity, sizeof(*buckets));
    if (!buckets) return ARK_LINK_ERR_MEMORY;
    for (size_t i = 0; i < index->capacity; i++) {
        item = index->buckets[i];
        while (item) { AbiRecord* next = item->next; bucket = (size_t)(item->hash & (capacity - 1)); item->next = buckets[bucket]; buckets[bucket] = item; item = next; }
    }
    free(index->buckets); index->buckets = buckets; index->capacity = capacity; return ARK_LINK_OK;
}

static const ArkLinkSection* symbol_section(const ArkLinkUnit* unit, const ArkSymbolDesc* symbol) {
    if (!symbol->section_index || symbol->section_index > unit->section_count) return NULL;
    return &unit->sections[symbol->section_index - 1];
}

static ArkLinkResult storage_record(AbiIndex* index,const ArkLinkUnit* unit,const ArkSymbolDesc* symbol) {
    if (!prefix(symbol->name,"_KRTG3$") && !prefix(symbol->name,"_KRTJ3$") && !prefix(symbol->name,"_KRTL3$")) return ARK_LINK_OK;
    int initializer=prefix(symbol->name,"_KRTL3$");const ArkLinkSection* section=symbol_section(unit,symbol);
    if ((symbol->binding!=ARK_BIND_GLOBAL && symbol->binding!=ARK_BIND_WEAK) ||
        symbol->type!=(initializer?ARK_SYM_FUNC:ARK_SYM_OBJECT) ||
        (symbol->section_index && (!section || section->kind!=(initializer?ARK_SECTION_CODE:ARK_SECTION_DATA) ||
            !range((size_t)symbol->value,symbol->size,section->size))) ||
        (!symbol->section_index && (symbol->binding!=ARK_BIND_GLOBAL || symbol->value || symbol->size)))
        return failure("E_ABI_MANIFEST",unit->path,"invalid native storage or initializer symbol");
    AbiRecord record={0};record.nominal=(const uint8_t*)symbol->name;record.nominal_size=(uint32_t)strlen(symbol->name);
    record.contract=record.nominal;record.kind=1;record.path=unit->path;record.defined=symbol->section_index!=0;
    record.weak=symbol->binding==ARK_BIND_WEAK;record.hash=hash_bytes(record.nominal,record.nominal_size);
    return insert(index,&record);
}

static ArkLinkResult read_manifest(AbiIndex* index, const ArkLinkUnit* unit, const ArkSymbolDesc* marker,
                                   int version, int types, uint8_t* required, uint8_t* recorded) {
    const ArkLinkSection* section = symbol_section(unit, marker);
    if (!section || section->kind != ARK_SECTION_RODATA || marker->binding != ARK_BIND_LOCAL ||
        marker->type != ARK_SYM_OBJECT || !range((size_t)marker->value, marker->size, section->size))
        return failure("E_ABI_MANIFEST", unit->path, "manifest must be one local read-only symbol");
    const uint8_t* bytes = section->data + marker->value;
    size_t size = marker->size;
    if (size < 16 || word(bytes) != UINT32_C(0x32494241) || word(bytes + 12) != size)
        return failure("E_ABI_MANIFEST", unit->path, "invalid native manifest header");
    if (word(bytes + 4) != (uint32_t)version)
        return failure("E_ABI_VERSION", unit->path, "unsupported native manifest version");
    uint32_t count = word(bytes + 8);
    if (count > (types ? 1048576 : unit->symbol_count))
        return failure("E_ABI_MANIFEST", unit->path, "invalid native record count");
    size_t position = 16;
    for (uint32_t i = 0; i < count; i++) {
        if (!range(position, 16, size)) return failure("E_ABI_MANIFEST", unit->path, "truncated native record");
        uint32_t referenced = word(bytes + position), flags = word(bytes + position + 4);
        uint32_t nominal_size = word(bytes + position + 8), contract_size = word(bytes + position + 12);
        position += 16;
        if (!range(position, nominal_size, size) || !range(position + nominal_size, contract_size, size))
            return failure("E_ABI_MANIFEST", unit->path, "native record exceeds manifest bounds");
        AbiRecord record = {0};
        record.nominal = bytes + position; record.contract = bytes + position + nominal_size;
        record.nominal_size = nominal_size; record.contract_size = contract_size; record.path = unit->path;
        if (types) {
            if (referenced != UINT32_MAX || flags != 2 || nominal_size < 18 || contract_size < (uint64_t)nominal_size + 12 ||
                memcmp(record.nominal,"_KRTT$",6) || memcmp(record.contract,"KRTTYPE3",8) ||
                memcmp(record.contract + 8,record.nominal,nominal_size))
                return failure("E_ABI_MANIFEST", unit->path, "invalid canonical type record");
            record.kind = 2; record.defined = 1;
        } else {
            if (referenced >= unit->symbol_count || (flags!=0&&flags!=1&&(flags!=5||version!=3)) || !required[referenced] || recorded[referenced] ||
                nominal_size < 6 || contract_size < (uint64_t)nominal_size + 18)
                return failure("E_ABI_MANIFEST", unit->path, "invalid or duplicate function contract record");
            const ArkSymbolDesc* symbol = &unit->symbols[referenced];
            const ArkLinkSection* code = symbol_section(unit,symbol);
            if (symbol->type != ARK_SYM_FUNC || symbol->binding != (flags==5?ARK_BIND_WEAK:ARK_BIND_GLOBAL) ||
                (flags && (!code || code->kind != ARK_SECTION_CODE || !range((size_t)symbol->value,symbol->size,code->size))) ||
                (!flags && (symbol->section_index || symbol->value || symbol->size)))
                return failure("E_ABI_MANIFEST", unit->path, "function record disagrees with symbol definition");
            int named = prefix(symbol->name,"_KRT3$");
            if ((named && version != 3) || memcmp(record.nominal,"_KRT1$",6) ||
                memcmp(record.contract,named?"KRTABI3":"KRTABI2",7) || memcmp(record.contract+7,record.nominal,nominal_size))
                return failure("E_ABI_MANIFEST", unit->path, "invalid canonical function identity");
            uint64_t digest = hash_bytes(record.contract,contract_size);
            size_t expected_size = 6 + nominal_size - 6 + 2 + 16;
            if (strlen(symbol->name) != expected_size || strncmp(symbol->name,named?"_KRT3$":"_KRT2$",6) ||
                memcmp(symbol->name+6,record.nominal+6,nominal_size-6) || memcmp(symbol->name+nominal_size,"$H",2))
                return failure("E_ABI_MANIFEST", unit->path, "invalid native fingerprint symbol");
            for (unsigned digit = 0; digit < 16; digit++) {
                unsigned value = (unsigned)(digest >> (60 - digit * 4)) & 15;
                if (symbol->name[nominal_size + 2 + digit] != (char)(value < 10 ? '0' + value : 'a' + value - 10))
                    return failure("E_ABI_MANIFEST", unit->path, "fingerprint disagrees with exact native contract");
            }
            record.defined = flags != 0; record.weak=flags==5; recorded[referenced] = 1;
        }
        record.hash = hash_bytes(record.nominal,nominal_size);
        ArkLinkResult result = insert(index,&record); if (result != ARK_LINK_OK) return result;
        position += (size_t)nominal_size + contract_size;
    }
    return position == size ? ARK_LINK_OK : failure("E_ABI_MANIFEST",unit->path,"trailing native manifest bytes");
}

typedef struct WeakRelocations {
    const ArkLinkSection* section;
    ArkRelocationDesc* sorted;
    struct WeakRelocations* next;
} WeakRelocations;
typedef struct WeakRecord {
    const ArkLinkUnit* unit;
    const ArkSymbolDesc* symbol;
    WeakRelocations* relocations;
    uint64_t hash;
    struct WeakRecord* next;
} WeakRecord;
typedef struct WeakIndex {
    WeakRecord** buckets;
    size_t capacity;
    size_t count;
    WeakRelocations* relocations;
} WeakIndex;

static int relocation_order(const void* a,const void* b) {
    const ArkRelocationDesc* x=a; const ArkRelocationDesc* y=b;
    return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}
static size_t relocation_start(const WeakRelocations* cache,uint64_t offset) {
    size_t first=0,last=cache->section->reloc_count;
    while(first<last) { size_t middle=first+(last-first)/2;
        if(cache->sorted[middle].offset<offset) first=middle+1; else last=middle; }
    return first;
}
static int weak_descriptor_equal(const WeakRecord* a,const WeakRecord* b) {
    const ArkLinkUnit* ua=a->unit; const ArkLinkUnit* ub=b->unit;
    const ArkSymbolDesc* sa=a->symbol; const ArkSymbolDesc* sb=b->symbol;
    const ArkLinkSection* x=a->relocations->section; const ArkLinkSection* y=b->relocations->section;
    if (!x || !y || sa->type != sb->type || x->kind != y->kind || sa->size != sb->size ||
        !range((size_t)sa->value,sa->size,x->size) || !range((size_t)sb->value,sb->size,y->size) ||
        memcmp(x->data+sa->value,y->data+sb->value,sa->size)) return 0;
    size_t i=relocation_start(a->relocations,sa->value),j=relocation_start(b->relocations,sb->value);
    size_t end=relocation_start(a->relocations,sa->value+sa->size),other_end=relocation_start(b->relocations,sb->value+sb->size);
    if(end-i!=other_end-j) return 0;
    while(i<end) {
        const ArkRelocationDesc* r=&a->relocations->sorted[i++];
        const ArkRelocationDesc* s=&b->relocations->sorted[j++];
        if(r->offset-sa->value!=s->offset-sb->value||r->type!=s->type||r->addend!=s->addend||
           r->sym_idx>=ua->symbol_count||s->sym_idx>=ub->symbol_count) return 0;
        const ArkSymbolDesc* from=&ua->symbols[r->sym_idx]; const ArkSymbolDesc* to=&ub->symbols[s->sym_idx];
        if(from->binding==ARK_BIND_LOCAL||to->binding==ARK_BIND_LOCAL||!from->name||!to->name||strcmp(from->name,to->name)) return 0;
    }
    return 1;
}
static void weak_destroy(WeakIndex* index) {
    for(size_t i=0;i<index->capacity;i++) {
        WeakRecord* item=index->buckets[i];while(item) {WeakRecord* next=item->next;free(item);item=next;}
    }
    free(index->buckets);WeakRelocations* cache=index->relocations;
    while(cache) {WeakRelocations* next=cache->next;free(cache->sorted);free(cache);cache=next;}
}
static ArkLinkResult weak_insert(WeakIndex* index,const ArkLinkUnit* unit,const ArkSymbolDesc* symbol) {
    const ArkLinkSection* section=symbol_section(unit,symbol);
    if(!section||section->kind!=ARK_SECTION_RODATA||symbol->type!=ARK_SYM_OBJECT||
       !range((size_t)symbol->value,symbol->size,section->size))
        return failure("E_ABI_LAYOUT",unit->path,"runtime descriptor must be bounded read-only data");
    WeakRelocations* cache=index->relocations;
    while(cache&&cache->section!=section) cache=cache->next;
    if(!cache) {
        cache=calloc(1,sizeof(*cache));if(!cache) return ARK_LINK_ERR_MEMORY;
        cache->section=section;cache->next=index->relocations;index->relocations=cache;
        if(section->reloc_count) {
            if(section->reloc_count>SIZE_MAX/sizeof(*cache->sorted)) return ARK_LINK_ERR_MEMORY;
            cache->sorted=malloc(section->reloc_count*sizeof(*cache->sorted));if(!cache->sorted) return ARK_LINK_ERR_MEMORY;
            memcpy(cache->sorted,section->relocs,section->reloc_count*sizeof(*cache->sorted));
            qsort(cache->sorted,section->reloc_count,sizeof(*cache->sorted),relocation_order);
            for(size_t i=1;i<section->reloc_count;i++) if(cache->sorted[i-1].offset==cache->sorted[i].offset)
                return failure("E_ABI_LAYOUT",unit->path,"runtime section has duplicate relocation offsets");
        }
    }
    WeakRecord candidate={unit,symbol,cache,hash_bytes((const uint8_t*)symbol->name,strlen(symbol->name)),NULL};
    size_t bucket=(size_t)(candidate.hash&(index->capacity-1));
    for(WeakRecord* item=index->buckets[bucket];item;item=item->next) {
        if(item->hash!=candidate.hash||strcmp(item->symbol->name,symbol->name)) continue;
        return weak_descriptor_equal(&candidate,item)?ARK_LINK_OK:
            failure("E_ABI_LAYOUT",unit->path,"weak runtime descriptors disagree exactly");
    }
    if(index->count>=1048576) return failure("E_ABI_MANIFEST",unit->path,"too many runtime descriptor identities");
    WeakRecord* item=malloc(sizeof(*item));if(!item) return ARK_LINK_ERR_MEMORY;
    *item=candidate;item->next=index->buckets[bucket];index->buckets[bucket]=item;index->count++;
    if(index->count<index->capacity*3/4||index->capacity>=1048576) return ARK_LINK_OK;
    size_t capacity=index->capacity*2;WeakRecord** buckets=calloc(capacity,sizeof(*buckets));if(!buckets) return ARK_LINK_ERR_MEMORY;
    for(size_t i=0;i<index->capacity;i++) {
        item=index->buckets[i];while(item) {WeakRecord* next=item->next;bucket=(size_t)(item->hash&(capacity-1));item->next=buckets[bucket];buckets[bucket]=item;item=next;}
    }
    free(index->buckets);index->buckets=buckets;index->capacity=capacity;return ARK_LINK_OK;
}

ArkLinkResult ark_native_abi_validate(ArkLinkUnit* const* units, size_t count) {
    AbiIndex index = {0}; index.capacity = 256; index.buckets = calloc(index.capacity,sizeof(*index.buckets));
    if (!index.buckets) return ARK_LINK_ERR_MEMORY;
    WeakIndex weak={0};weak.capacity=256;weak.buckets=calloc(weak.capacity,sizeof(*weak.buckets));
    if(!weak.buckets) {index_destroy(&index);return ARK_LINK_ERR_MEMORY;}
    ArkLinkResult result = ARK_LINK_OK;
    for (size_t u = 0; u < count && result == ARK_LINK_OK; u++) {
        const ArkLinkUnit* unit = units[u]; if (!unit) continue;
        uint8_t* required = calloc(unit->symbol_count ? unit->symbol_count : 1,1);
        uint8_t* recorded = calloc(unit->symbol_count ? unit->symbol_count : 1,1);
        const ArkSymbolDesc* markers[3] = {NULL,NULL,NULL};
        if (!required || !recorded) { free(required); free(recorded); result=ARK_LINK_ERR_MEMORY; break; }
        for (size_t i = 0; i < unit->symbol_count && result == ARK_LINK_OK; i++) {
            const ArkSymbolDesc* symbol = &unit->symbols[i];
            if (!symbol->name) { result=failure("E_ABI_MANIFEST",unit->path,"invalid object symbol string"); break; }
            int marker = strcmp(symbol->name,"_KRT_ABI2_MANIFEST")==0 ? 0 :
                strcmp(symbol->name,"_KRT_ABI3_MANIFEST")==0 ? 1 : strcmp(symbol->name,"_KRT_ABI3_TYPES")==0 ? 2 : -1;
            if (marker >= 0) {
                if (markers[marker]) { result=failure("E_ABI_MANIFEST",unit->path,"duplicate native manifest"); break; }
                markers[marker]=symbol;
            }
            required[i] = (uint8_t)(prefix(symbol->name,"_KRT2$") || prefix(symbol->name,"_KRT3$"));
            if (symbol->section_index && symbol->binding == ARK_BIND_WEAK &&
                (prefix(symbol->name,"_KRTD3$")||prefix(symbol->name,"_KRTK3$")||prefix(symbol->name,"_KRTI3$")||prefix(symbol->name,"_KRTM3$"))) {
                result=weak_insert(&weak,unit,symbol);
            }
        }
        for (int i = 0; i < 3 && result == ARK_LINK_OK; i++) if (markers[i])
            result=read_manifest(&index,unit,markers[i],i==0?2:3,i==2,required,recorded);
        for (size_t i=0;i<unit->symbol_count && result==ARK_LINK_OK;i++)
            result=storage_record(&index,unit,&unit->symbols[i]);
        for (size_t i = 0; i < unit->symbol_count && result == ARK_LINK_OK; i++) if (required[i] && !recorded[i])
            result=failure("E_ABI_MANIFEST",unit->path,"native symbol is missing its exact contract");
        free(required); free(recorded);
    }
    for (size_t i = 0; i < index.capacity && result == ARK_LINK_OK; i++)
        for (AbiRecord* record=index.buckets[i];record;record=record->next) if (record->kind!=2 && !record->defined) {
            result=failure("E_ABI_SYMBOL",record->path,record->kind==1?"missing native storage or initializer definition":"missing native function definition"); break;
        }
    weak_destroy(&weak);index_destroy(&index); return result;
}
