#pragma once


#include "engine/lumix.h"
#include "engine/metaprogramming.h"
#include "engine/resource.h"
#include "engine/stream.h"
#include "engine/universe.h"


#define LUMIX_REFL_SCENE(Class, Label) using ReflScene = Class; reflection::build_scene(Label)
#define LUMIX_REFL_FUNC_EX(F, Name) function<&ReflScene::F>(Name, #F)
#define LUMIX_REFL_FUNC(F) function<&ReflScene::F>(#F, #F)
#define LUMIX_REFL_CMP(Cmp, Name, Label) cmp<&ReflScene::create##Cmp, &ReflScene::destroy##Cmp>(Name, Label)
#define LUMIX_REFL_PROP(Property, Label) prop<&ReflScene::get##Property, &ReflScene::set##Property>(Label)
#define LUMIX_ENUM_REFL_PROP(Property, Label) enum_prop<&ReflScene::get##Property, &ReflScene::set##Property>(Label)

#define LUMIX_FUNC_EX(Func, Name) reflection::function(&Func, #Func, Name)
#define LUMIX_FUNC(Func) reflection::function(&Func, #Func, nullptr)

namespace Lumix
{

template <typename T> struct Array;
template <typename T> struct Span;
struct Path;
struct IVec3;
struct Vec2;
struct Vec3;
struct Vec4;

namespace reflection
{


struct IAttribute {
	enum Type {
		MIN,
		CLAMP,
		RADIANS,
		COLOR,
		RESOURCE,
		ENUM,
		MULTILINE,
		STRING_ENUM,
		NO_UI,
	};

	virtual ~IAttribute() {}
	virtual int getType() const = 0;
};

struct ComponentBase;

// we don't use method pointers here because VS has sizeof issues if IScene is forward declared
using CreateComponent = void (*)(IScene*, EntityRef);
using DestroyComponent = void (*)(IScene*, EntityRef);

struct RegisteredReflComponent {
	u32 name_hash = 0;
	u32 scene = 0;
	struct reflcmp* cmp = nullptr;
};

LUMIX_ENGINE_API const reflcmp* getReflComponent(ComponentType cmp_type);
LUMIX_ENGINE_API const ComponentBase* getComponent(ComponentType cmp_type);
LUMIX_ENGINE_API const struct reflprop* getReflProp(ComponentType cmp_type, const char* prop);
LUMIX_ENGINE_API Span<const RegisteredReflComponent> getReflComponents();

LUMIX_ENGINE_API ComponentType getComponentType(const char* id);
LUMIX_ENGINE_API ComponentType getComponentTypeFromHash(u32 hash);

template <typename T> void writeToStream(OutputMemoryStream& stream, T value) {	stream.write(value); }
template <typename T> T readFromStream(InputMemoryStream& stream) { return stream.read<T>(); }
template <> LUMIX_ENGINE_API Path readFromStream<Path>(InputMemoryStream& stream);
template <> LUMIX_ENGINE_API void writeToStream<Path>(OutputMemoryStream& stream, Path);
template <> LUMIX_ENGINE_API void writeToStream<const Path&>(OutputMemoryStream& stream, const Path& path);
template <> LUMIX_ENGINE_API const char* readFromStream<const char*>(InputMemoryStream& stream);
template <> LUMIX_ENGINE_API void writeToStream<const char*>(OutputMemoryStream& stream, const char* path);
	
struct ResourceAttribute : IAttribute
{
	ResourceAttribute(ResourceType type) : resource_type(type) {}
	ResourceAttribute() {}

	int getType() const override { return RESOURCE; }

	ResourceType resource_type;
};

struct MinAttribute : IAttribute
{
	explicit MinAttribute(float min) { this->min = min; }
	MinAttribute() {}

	int getType() const override { return MIN; }

	float min;
};

struct ClampAttribute : IAttribute
{
	ClampAttribute(float min, float max) { this->min = min; this->max = max; }
	ClampAttribute() {}

	int getType() const override { return CLAMP; }

	float min;
	float max;
};

struct EnumAttribute : IAttribute {
	virtual u32 count(ComponentUID cmp) const = 0;
	virtual const char* name(ComponentUID cmp, u32 idx) const = 0;
	
	int getType() const override { return ENUM; }
};

struct StringEnumAttribute : IAttribute {
	virtual u32 count(ComponentUID cmp) const = 0;
	virtual const char* name(ComponentUID cmp, u32 idx) const = 0;
	
	int getType() const override { return STRING_ENUM; }
};

struct RadiansAttribute : IAttribute
{
	int getType() const override { return RADIANS; }
};

struct MultilineAttribute : IAttribute
{
	int getType() const override { return MULTILINE; }
};

struct ColorAttribute : IAttribute
{
	int getType() const override { return COLOR; }
};

struct NoUIAttribute : IAttribute {
	int getType() const override { return NO_UI; }
};

struct PropertyTag {};

template <typename T> struct Property : PropertyTag {
	virtual Span<const IAttribute* const> getAttributes() const = 0;
	virtual T get(ComponentUID cmp, int index) const = 0;
	virtual void set(ComponentUID cmp, int index, T) const = 0;
	const char* name;
};

struct IBlobProperty : PropertyTag  {
	virtual void getValue(ComponentUID cmp, int index, OutputMemoryStream& stream) const = 0;
	virtual void setValue(ComponentUID cmp, int index, InputMemoryStream& stream) const = 0;
	const char* name;
};

struct IDynamicProperties : PropertyTag  {
	enum Type {
		I32,
		FLOAT,
		STRING,
		ENTITY,
		RESOURCE,
		BOOLEAN,
		COLOR,

		NONE
	};
	union Value {
		Value(){}
		EntityPtr e;
		i32 i;
		float f;
		const char* s;
		bool b;
		Vec3 v3;
	};
	virtual u32 getCount(ComponentUID cmp, int array_idx) const = 0;
	virtual Type getType(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual const char* getName(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual Value getValue(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual reflection::ResourceAttribute getResourceAttribute(ComponentUID cmp, int array_idx, u32 idx) const = 0;
	virtual void set(ComponentUID cmp, int array_idx, const char* name, Type type, Value value) const = 0;
	virtual void set(ComponentUID cmp, int array_idx, u32 idx, Value value) const = 0;

	const char* name;
};

template <typename T> inline T get(IDynamicProperties::Value);
template <> inline float get(IDynamicProperties::Value v) { return v.f; }
template <> inline i32 get(IDynamicProperties::Value v) { return v.i; }
template <> inline const char* get(IDynamicProperties::Value v) { return v.s; }
template <> inline EntityPtr get(IDynamicProperties::Value v) { return v.e; }
template <> inline bool get(IDynamicProperties::Value v) { return v.b; }
template <> inline Vec3 get(IDynamicProperties::Value v) { return v.v3; }

template <typename T> inline void set(IDynamicProperties::Value& v, T);
template <> inline void set(IDynamicProperties::Value& v, float val) { v.f = val; }
template <> inline void set(IDynamicProperties::Value& v, i32 val) { v.i = val; }
template <> inline void set(IDynamicProperties::Value& v, const char* val) { v.s = val; }
template <> inline void set(IDynamicProperties::Value& v, EntityPtr val) { v.e = val; }
template <> inline void set(IDynamicProperties::Value& v, bool val) { v.b = val; }
template <> inline void set(IDynamicProperties::Value& v, Vec3 val) { v.v3 = val; }

struct IArrayProperty : PropertyTag 
{
	virtual void addItem(ComponentUID cmp, int index) const = 0;
	virtual void removeItem(ComponentUID cmp, int index) const = 0;
	virtual int getCount(ComponentUID cmp) const = 0;
	virtual void visit(struct IPropertyVisitor& visitor) const = 0;

	const char* name;
};

struct reflprop {
	reflprop(IAllocator& allocator) : attributes(allocator) {}
	Array<reflection::IAttribute*> attributes;

	virtual void visit(struct IReflPropertyVisitor& visitor) const = 0;
	const char* name;
};


template <typename T>
struct refl_typed_prop : reflprop {
	refl_typed_prop(IAllocator& allocator) : reflprop(allocator) {}

	typedef void (*setter_t)(IScene*, EntityRef, u32, const T&);
	typedef T (*getter_t)(IScene*, EntityRef, u32);

	template <auto F>
	static void setterHelper(IScene* scene, EntityRef e, u32 idx, const T& value) {
		using C = typename ClassOf<decltype(F)>::Type;
		if constexpr (ArgsCount<decltype(F)>::value == 2) {
			(static_cast<C*>(scene)->*F)(e, value);
		}
		else {
			(static_cast<C*>(scene)->*F)(e, idx, value);
		}
	}

	template <auto F>
	static T getterHelper(IScene* scene, EntityRef e, u32 idx) {
		using C = typename ClassOf<decltype(F)>::Type;
		if constexpr (ArgsCount<decltype(F)>::value == 1) {
			return (static_cast<C*>(scene)->*F)(e);
		}
		else {
			return (static_cast<C*>(scene)->*F)(e, idx);
		}
	}

	template <auto Getter, auto PropGetter>
	static void setterVarHelper(IScene* scene, EntityRef e, u32, const T& value) {
		using C = typename ClassOf<decltype(Getter)>::Type;
		auto& c = (static_cast<C*>(scene)->*Getter)(e);
		auto& v = c.*PropGetter;
		v = value;
	}

	template <auto Getter, auto PropGetter>
	static T getterVarHelper(IScene* scene, EntityRef e, u32) {
		using C = typename ClassOf<decltype(Getter)>::Type;
		auto& c = (static_cast<C*>(scene)->*Getter)(e);
		auto& v = c.*PropGetter;
		return static_cast<T>(v);
	}

	void visit(IReflPropertyVisitor& visitor) const override {
		visitor.visit(*this);
	}

	T get(ComponentUID cmp, u32 idx) const {
		return getter(cmp.scene, (EntityRef)cmp.entity, idx);
	}

	void set(ComponentUID cmp, u32 idx, const T& val) const {
		setter(cmp.scene, (EntityRef)cmp.entity, idx, val);
	}

	setter_t setter;
	getter_t getter;
};

struct IReflPropertyVisitor {
	virtual ~IReflPropertyVisitor() {}
	virtual void visit(const refl_typed_prop<float>& prop) = 0;
	virtual void visit(const refl_typed_prop<int>& prop) = 0;
	virtual void visit(const refl_typed_prop<u32>& prop) = 0;
	virtual void visit(const refl_typed_prop<EntityPtr>& prop) = 0;
	virtual void visit(const refl_typed_prop<Vec2>& prop) = 0;
	virtual void visit(const refl_typed_prop<Vec3>& prop) = 0;
	virtual void visit(const refl_typed_prop<IVec3>& prop) = 0;
	virtual void visit(const refl_typed_prop<Vec4>& prop) = 0;
	virtual void visit(const refl_typed_prop<Path>& prop) = 0;
	virtual void visit(const refl_typed_prop<bool>& prop) = 0;
	virtual void visit(const refl_typed_prop<const char*>& prop) = 0;
	virtual void visit(const struct reflarrayprop& prop) = 0;
};

struct IReflEmptyPropertyVisitor : IReflPropertyVisitor {
	virtual ~IReflEmptyPropertyVisitor() {}
	void visit(const refl_typed_prop<float>& prop) override {}
	void visit(const refl_typed_prop<int>& prop) override {}
	void visit(const refl_typed_prop<u32>& prop) override {}
	void visit(const refl_typed_prop<EntityPtr>& prop) override {}
	void visit(const refl_typed_prop<Vec2>& prop) override {}
	void visit(const refl_typed_prop<Vec3>& prop) override {}
	void visit(const refl_typed_prop<IVec3>& prop) override {}
	void visit(const refl_typed_prop<Vec4>& prop) override {}
	void visit(const refl_typed_prop<Path>& prop) override {}
	void visit(const refl_typed_prop<bool>& prop) override {}
	void visit(const refl_typed_prop<const char*>& prop) override {}
	void visit(const reflarrayprop& prop) override {}
};
struct reflarrayprop : reflprop {
	reflarrayprop(IAllocator& allocator)
		: reflprop(allocator)
		, children(allocator)
	{}

	u32 getCount(ComponentUID cmp) const {
		return counter(cmp.scene, (EntityRef)cmp.entity);
	}

	void addItem(ComponentUID cmp, u32 idx) const {
		adder(cmp.scene, (EntityRef)cmp.entity, idx);
	}

	void removeItem(ComponentUID cmp, u32 idx) const {
		remover(cmp.scene, (EntityRef)cmp.entity, idx);
	}

	typedef u32 (*counter_t)(IScene*, EntityRef);
	typedef void (*adder_t)(IScene*, EntityRef, u32);
	typedef void (*remover_t)(IScene*, EntityRef, u32);

	void visit(struct IReflPropertyVisitor& visitor) const override {
		visitor.visit(*this);
	}

	void visitChildren(struct IReflPropertyVisitor& visitor) const {
		for (reflprop* prop : children) {
			prop->visit(visitor);
		}
	}

	Array<reflprop*> children;
	counter_t counter;
	adder_t adder;
	remover_t remover;
};


struct IPropertyVisitor
{
	virtual ~IPropertyVisitor() {}

	virtual void visit(const Property<float>& prop) = 0;
	virtual void visit(const Property<int>& prop) = 0;
	virtual void visit(const Property<u32>& prop) = 0;
	virtual void visit(const Property<EntityPtr>& prop) = 0;
	virtual void visit(const Property<Vec2>& prop) = 0;
	virtual void visit(const Property<Vec3>& prop) = 0;
	virtual void visit(const Property<IVec3>& prop) = 0;
	virtual void visit(const Property<Vec4>& prop) = 0;
	virtual void visit(const Property<Path>& prop) = 0;
	virtual void visit(const Property<bool>& prop) = 0;
	virtual void visit(const Property<const char*>& prop) = 0;
	virtual void visit(const IDynamicProperties& prop) {}
	virtual void visit(const IArrayProperty& prop) = 0;
	virtual void visit(const IBlobProperty& prop) = 0;
};

struct IEmptyPropertyVisitor : IPropertyVisitor
{
	void visit(const Property<float>& prop) override {}
	void visit(const Property<int>& prop) override {}
	void visit(const Property<u32>& prop) override {}
	void visit(const Property<EntityPtr>& prop) override {}
	void visit(const Property<Vec2>& prop) override {}
	void visit(const Property<Vec3>& prop) override {}
	void visit(const Property<IVec3>& prop) override {}
	void visit(const Property<Vec4>& prop) override {}
	void visit(const Property<Path>& prop) override {}
	void visit(const Property<bool>& prop) override {}
	void visit(const Property<const char*>& prop) override {}
	void visit(const IArrayProperty& prop) override {}
	void visit(const IBlobProperty& prop) override {}
	void visit(const IDynamicProperties& prop) override {}
};

struct ComponentBase
{
	virtual void visit(IPropertyVisitor&) const = 0;
	virtual Span<const struct FunctionBase* const> getFunctions() const = 0;
};

struct Icon { const char* name; };
inline Icon icon(const char* name) { return {name}; }

template <typename T>
bool getPropertyValue(IScene& scene, EntityRef e, ComponentType cmp_type, const char* prop_name, Ref<T> out) {
	struct : IReflEmptyPropertyVisitor {
		void visit(const refl_typed_prop<T>& prop) override {
			if (equalStrings(prop.name, prop_name)) {
				found = true;
				value = prop.get(cmp, -1);
			}
		}
		ComponentUID cmp;
		const char* prop_name;
		T value = {};
		bool found = false;
	} visitor;
	visitor.prop_name = prop_name;
	visitor.cmp.scene = &scene;
	visitor.cmp.type = cmp_type;
	visitor.cmp.entity = e;
	const reflection::reflcmp* cmp_desc = getReflComponent(cmp_type);
	cmp_desc->visit(visitor);
	out = visitor.value;
	return visitor.found;
}

template <typename Base, typename... T>
struct TupleHolder {
	TupleHolder() {}
	
	TupleHolder(Tuple<T...> tuple) { init(tuple); }
	TupleHolder(TupleHolder&& rhs) { init(rhs.objects); }
	TupleHolder(const TupleHolder& rhs) { init(rhs.objects); }

	void init(const Tuple<T...>& tuple)
	{
		objects = tuple;
		int i = 0;
		apply([&](auto& v){
			ptrs[i] = &v;
			++i;
		}, objects);
	}

	void operator =(Tuple<T...>&& tuple) { init(tuple); }
	void operator =(const TupleHolder& rhs) { init(rhs.objects); }
	void operator =(TupleHolder&& rhs) { init(rhs.objects); }

	Span<const Base* const> get() const {
		return Span(static_cast<const Base*const*>(ptrs), (u32)sizeof...(T));
	}

	Span<Base* const> get() {
		return Span(static_cast<Base*const*>(ptrs), (u32)sizeof...(T));
	}

	Tuple<T...> objects;
	Base* ptrs[sizeof...(T) + 1];
};

template <typename Getter, typename Setter>
struct BlobProperty : IBlobProperty
{
	void getValue(ComponentUID cmp, int index, OutputMemoryStream& stream) const override
	{
		using C = typename ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*getter)((EntityRef)cmp.entity, stream);
	}

	void setValue(ComponentUID cmp, int index, InputMemoryStream& stream) const override
	{
		using C = typename ClassOf<Getter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		(inst->*setter)((EntityRef)cmp.entity, stream);
	}

	Getter getter;
	Setter setter;
};


template <typename T, typename CmpGetter, typename PtrType, typename... Attributes>
struct VarProperty : Property<T>
{
	Span<const IAttribute* const> getAttributes() const override { return attributes.get(); }

	T get(ComponentUID cmp, int index) const override
	{
		using C = typename ClassOf<CmpGetter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		auto& c = (inst->*cmp_getter)((EntityRef)cmp.entity);
		auto& v = c.*ptr;
		return static_cast<T>(v);
	}

	void set(ComponentUID cmp, int index, T value) const override
	{
		using C = typename ClassOf<CmpGetter>::Type;
		C* inst = static_cast<C*>(cmp.scene);
		auto& c = (inst->*cmp_getter)((EntityRef)cmp.entity);
		c.*ptr = value;
	}

	CmpGetter cmp_getter;
	PtrType ptr;
	TupleHolder<IAttribute, Attributes...> attributes;
};

namespace detail {

static const unsigned int FRONT_SIZE = sizeof("Lumix::reflection::detail::GetTypeNameHelper<") - 1u;
static const unsigned int BACK_SIZE = sizeof(">::GetTypeName") - 1u;

template <typename T>
struct GetTypeNameHelper
{
	static const char* GetTypeName()
	{
		#if defined(_MSC_VER) && !defined(__clang__)
			static const size_t size = sizeof(__FUNCTION__) - FRONT_SIZE - BACK_SIZE;
			static char typeName[size] = {};
			memcpy(typeName, __FUNCTION__ + FRONT_SIZE, size - 1u); //-V594
		#else
			static const size_t size = sizeof(__PRETTY_FUNCTION__) - FRONT_SIZE - BACK_SIZE;
			static char typeName[size] = {};
			memcpy(typeName, __PRETTY_FUNCTION__ + FRONT_SIZE, size - 1u);
		#endif
		return typeName;
	}
};

} // namespace detail


template <typename T>
const IAttribute* getAttribute(const Property<T>& prop, IAttribute::Type type) {
	for (const IAttribute* attr : prop.getAttributes()) {
		if (attr->getType() == type) return attr;
	}
	return nullptr;
}

template <typename T>
const IAttribute* getAttribute(const refl_typed_prop<T>& prop, IAttribute::Type type) {
	for (const IAttribute* attr : prop.attributes) {
		if (attr->getType() == type) return attr;
	}
	return nullptr;
}

template <typename T>
const char* getTypeName()
{
	return detail::GetTypeNameHelper<T>::GetTypeName();
}

struct Variant {
	Variant() { type = I32; i = 0; }
	enum Type {
		VOID,
		PTR,
		BOOL,
		I32,
		U32,
		FLOAT,
		CSTR,
		ENTITY,
		VEC2,
		VEC3,
		DVEC3
	} type;
	union {
		bool b;
		i32 i;
		u32 u;
		float f;
		const char* s;
		EntityPtr e;
		Vec2 v2;
		Vec3 v3;
		DVec3 dv3;
		void* ptr;
	};

	void operator =(bool v) { b = v; type = BOOL; }
	void operator =(i32 v) { i = v; type = I32; }
	void operator =(u32 v) { u = v; type = U32; }
	void operator =(float v) { f = v; type = FLOAT; }
	void operator =(const char* v) { s = v; type = CSTR; }
	void operator =(EntityPtr v) { e = v; type = ENTITY; }
	void operator =(Vec2 v) { v2 = v; type = VEC2; }
	void operator =(Vec3 v) { v3 = v; type = VEC3; }
	void operator =(const DVec3& v) { dv3 = v; type = DVEC3; }
	void operator =(void* v) { ptr = v; type = PTR; }
};

struct FunctionBase
{
	virtual ~FunctionBase() {}

	virtual u32 getArgCount() const = 0;
	virtual Variant::Type getReturnType() const = 0;
	virtual const char* getReturnTypeName() const = 0;
	virtual const char* getThisTypeName() const = 0;
	virtual Variant::Type getArgType(int i) const = 0;
	virtual Variant invoke(void* obj, Span<Variant> args) const = 0;

	const char* decl_code;
	const char* name;
};

template <typename T> struct VariantTag {};

template <typename T> inline Variant::Type _getVariantType(VariantTag<T*>) { return Variant::PTR; }
inline Variant::Type _getVariantType(VariantTag<void>) { return Variant::VOID; }
inline Variant::Type _getVariantType(VariantTag<bool>) { return Variant::BOOL; }
inline Variant::Type _getVariantType(VariantTag<i32>) { return Variant::I32; }
inline Variant::Type _getVariantType(VariantTag<u32>) { return Variant::U32; }
inline Variant::Type _getVariantType(VariantTag<float>) { return Variant::FLOAT; }
inline Variant::Type _getVariantType(VariantTag<const char*>) { return Variant::CSTR; }
inline Variant::Type _getVariantType(VariantTag<EntityPtr>) { return Variant::ENTITY; }
inline Variant::Type _getVariantType(VariantTag<EntityRef>) { return Variant::ENTITY; }
inline Variant::Type _getVariantType(VariantTag<Vec2>) { return Variant::VEC2; }
inline Variant::Type _getVariantType(VariantTag<Vec3>) { return Variant::VEC3; }
inline Variant::Type _getVariantType(VariantTag<Path>) { return Variant::CSTR; }
inline Variant::Type _getVariantType(VariantTag<DVec3>) { return Variant::DVEC3; }
template <typename T> inline Variant::Type getVariantType() { return _getVariantType(VariantTag<RemoveCVR<T>>{}); }

inline bool fromVariant(int i, Span<Variant> args, VariantTag<bool>) { return args[i].b; }
inline float fromVariant(int i, Span<Variant> args, VariantTag<float>) { return args[i].f; }
inline const char* fromVariant(int i, Span<Variant> args, VariantTag<const char*>) { return args[i].s; }
inline Path fromVariant(int i, Span<Variant> args, VariantTag<Path>) { return Path(args[i].s); }
inline i32 fromVariant(int i, Span<Variant> args, VariantTag<i32>) { return args[i].i; }
inline u32 fromVariant(int i, Span<Variant> args, VariantTag<u32>) { return args[i].u; }
inline Vec2 fromVariant(int i, Span<Variant> args, VariantTag<Vec2>) { return args[i].v2; }
inline Vec3 fromVariant(int i, Span<Variant> args, VariantTag<Vec3>) { return args[i].v3; }
inline DVec3 fromVariant(int i, Span<Variant> args, VariantTag<DVec3>) { return args[i].dv3; }
inline EntityPtr fromVariant(int i, Span<Variant> args, VariantTag<EntityPtr>) { return args[i].e; }
inline EntityRef fromVariant(int i, Span<Variant> args, VariantTag<EntityRef>) { return (EntityRef)args[i].e; }
inline void* fromVariant(int i, Span<Variant> args, VariantTag<void*>) { return args[i].ptr; }
template <typename T> inline T* fromVariant(int i, Span<Variant> args, VariantTag<T*>) { return (T*)args[i].ptr; }

template <typename... Args>
struct VariantCaller {
	template <typename C, typename F, int... I>
	static Variant call(C* inst, F f, Span<Variant> args, Indices<I...>& indices) {
		using R = typename ResultOf<F>::Type;
		if constexpr (IsSame<R, void>::Value) {
			Variant v;
			v.type = Variant::VOID;
			(inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
			return v;
		}
		else {
			Variant v;
			v = (inst->*f)(fromVariant(I, args, VariantTag<RemoveCVR<Args>>{})...);
			return v;
		}
	}
};

template <typename F> struct Function;

template <typename R, typename C, typename... Args>
struct Function<R (C::*)(Args...)> : FunctionBase
{
	using F = R(C::*)(Args...);
	F function;

	u32 getArgCount() const override { return sizeof...(Args); }
	Variant::Type getReturnType() const override { return getVariantType<R>(); }
	const char* getReturnTypeName() const override { return getTypeName<R>(); }
	const char* getThisTypeName() const override { return getTypeName<C>(); }
	
	Variant::Type getArgType(int i) const override
	{
		Variant::Type expand[] = {
			getVariantType<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}
	
	Variant invoke(void* obj, Span<Variant> args) const override {
		auto indices = typename BuildIndices<-1, sizeof...(Args)>::result{};
		return VariantCaller<Args...>::call((C*)obj, function, args, indices);
	}
};

template <typename R, typename C, typename... Args>
struct Function<R (C::*)(Args...) const> : FunctionBase
{
	using F = R(C::*)(Args...) const;
	F function;

	u32 getArgCount() const override { return sizeof...(Args); }
	Variant::Type getReturnType() const override { return getVariantType<R>(); }
	const char* getReturnTypeName() const override { return getTypeName<R>(); }
	const char* getThisTypeName() const override { return getTypeName<C>(); }
	
	Variant::Type getArgType(int i) const override
	{
		Variant::Type expand[] = {
			getVariantType<Args>()...,
			Variant::Type::VOID
		};
		return expand[i];
	}

	Variant invoke(void* obj, Span<Variant> args) const override {
		auto indices = typename BuildIndices<-1, sizeof...(Args)>::result{};
		return VariantCaller<Args...>::call((const C*)obj, function, args, indices);
	}
};

LUMIX_ENGINE_API Array<FunctionBase*>& allFunctions();

template <typename F>
auto& function(F func, const char* decl_code, const char* name)
{
	static Function<F> ret;
	allFunctions().push(&ret);
	ret.function = func;
	ret.decl_code = decl_code;
	ret.name = name;
	return ret;
}


struct reflcmp {
	reflcmp(IAllocator& allocator)
		: props(allocator)
		, functions(allocator)
	{}

	void visit(IReflPropertyVisitor& visitor) const {
		for (const reflprop* prop : props) {
			prop->visit(visitor);
		}
	}

	const char* icon = "";
	const char* name;
	const char* label;

	u32 scene;
	CreateComponent creator;
	DestroyComponent destroyer;
	ComponentType component_type;
	Array<reflprop*> props;
	Array<reflection::FunctionBase*> functions;
};

struct reflscene {
	reflscene(IAllocator& allocator)
		: cmps(allocator)
		, functions(allocator)
	{}

	Array<reflection::FunctionBase*> functions;
	Array<reflcmp*> cmps;
	const char* name;
	reflscene* next = nullptr;
};

struct builder {
	builder(IAllocator& allocator)
		: allocator(allocator)
	{
		scene = LUMIX_NEW(allocator, reflscene)(allocator);
	}

	template <auto Creator, auto Destroyer>
	builder& cmp(const char* name, const char* label) {
		auto creator = [](IScene* scene, EntityRef e){ (scene->*static_cast<void (IScene::*)(EntityRef)>(Creator))(e); };
		auto destroyer = [](IScene* scene, EntityRef e){ (scene->*static_cast<void (IScene::*)(EntityRef)>(Destroyer))(e); };
	
		reflcmp* cmp = LUMIX_NEW(allocator, reflcmp)(allocator);
		cmp->name = name;
		cmp->label = label;
		cmp->component_type = getComponentType(name);
		cmp->creator = creator;
		cmp->destroyer = destroyer;
		cmp->scene = crc32(scene->name);
		registerCmp(cmp);

		scene->cmps.push(cmp);
		return *this;
	}

	void registerCmp(reflcmp* cmp);

	template <auto Getter, auto Setter>
	builder& enum_prop(const char* name) {
		auto* p = LUMIX_NEW(allocator, refl_typed_prop<i32>)(allocator);
		p->setter = [](IScene* scene, EntityRef e, u32 idx, const i32& value) {
			using T = typename ResultOf<decltype(Getter)>::Type;
			using C = typename ClassOf<decltype(Setter)>::Type;
			if constexpr (ArgsCount<decltype(Setter)>::value == 2) {
				(static_cast<C*>(scene)->*Setter)(e, static_cast<T>(value));
			}
			else {
				(static_cast<C*>(scene)->*Setter)(e, idx, static_cast<T>(value));
			}
		};

		p->getter = [](IScene* scene, EntityRef e, u32 idx) -> i32 {
			using T = typename ResultOf<decltype(Getter)>::Type;
			using C = typename ClassOf<decltype(Getter)>::Type;
			if constexpr (ArgsCount<decltype(Getter)>::value == 1) {
				return static_cast<i32>((static_cast<C*>(scene)->*Getter)(e));
			}
			else {
				return static_cast<i32>((static_cast<C*>(scene)->*Getter)(e, idx));
			}
		};
		p->name = name;
		if (array) {
			array->children.push(p);
		}
		else {
			scene->cmps.back()->props.push(p);
		}
		last_prop = p;
		return *this;
	}

	template <auto Getter, auto Setter>
	builder& prop(const char* name) {
		using T = typename ResultOf<decltype(Getter)>::Type;
		auto* p = LUMIX_NEW(allocator, refl_typed_prop<T>)(allocator);
		p->setter = &refl_typed_prop<T>::setterHelper<Setter>;
		p->getter = &refl_typed_prop<T>::getterHelper<Getter>;
		p->name = name;
		if (array) {
			array->children.push(p);
		}
		else {
			scene->cmps.back()->props.push(p);
		}
		last_prop = p;
		return *this;
	}

	template <auto Getter, auto PropGetter>
	builder& var_prop(const char* name) {
		using T = typename ResultOf<decltype(PropGetter)>::Type;
		auto* p = LUMIX_NEW(allocator, refl_typed_prop<T>)(allocator);
		p->setter = &refl_typed_prop<T>::setterVarHelper<Getter, PropGetter>;
		p->getter = &refl_typed_prop<T>::getterVarHelper<Getter, PropGetter>;
		p->name = name;
		if (array) {
			array->children.push(p);
		}
		else {
			scene->cmps.back()->props.push(p);
		}
		last_prop = p;
		return *this;
	}

	builder& minAttribute(float value) {
		auto* a = LUMIX_NEW(allocator, reflection::MinAttribute)(value);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& clampAttribute(float min, float max) {
		auto* a = LUMIX_NEW(allocator, reflection::ClampAttribute)(min, max);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& resourceAttribute(ResourceType type) {
		auto* a = LUMIX_NEW(allocator, reflection::ResourceAttribute)(type);
		last_prop->attributes.push(a);
		return *this;
	}

	template <auto Counter, auto Adder, auto Remover>
	builder& begin_array(const char* name) {
		reflarrayprop* prop = LUMIX_NEW(allocator, reflarrayprop)(allocator);
		using C = typename ClassOf<decltype(Counter)>::Type;
		prop->counter = [](IScene* scene, EntityRef e) -> u32 {
			return (static_cast<C*>(scene)->*Counter)(e);
		};
		prop->adder = [](IScene* scene, EntityRef e, u32 idx) {
			(static_cast<C*>(scene)->*Adder)(e, idx);
		};
		prop->remover = [](IScene* scene, EntityRef e, u32 idx) {
			(static_cast<C*>(scene)->*Remover)(e, idx);
		};
		prop->name = name;
		scene->cmps.back()->props.push(prop);
		array = prop;
		last_prop = prop;
		return *this;
	}

	builder& end_array() {
		array = nullptr;
		last_prop = nullptr;
		return *this;
	}

	template <typename T>
	builder& enumAttribute() {
		auto* a = LUMIX_NEW(allocator, T);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& radiansAttribute() {
		auto* a = LUMIX_NEW(allocator, reflection::RadiansAttribute);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& colorAttribute() {
		auto* a = LUMIX_NEW(allocator, reflection::ColorAttribute);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& noUIAttribute() {
		auto* a = LUMIX_NEW(allocator, reflection::NoUIAttribute);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& multilineAttribute() {
		auto* a = LUMIX_NEW(allocator, reflection::MultilineAttribute);
		last_prop->attributes.push(a);
		return *this;
	}

	builder& icon(const char* icon) {
		scene->cmps.back()->icon = icon;
		return *this;
	}

	template <auto F>
	builder& function(const char* name, const char* decl_code) {
		auto* f = LUMIX_NEW(allocator, reflection::Function<decltype(F)>);
		f->function = F;
		f->name = name;
		f->decl_code = decl_code;
		if (scene->cmps.empty()) {
			scene->functions.push(f);
		}
		else {
			scene->cmps.back()->functions.push(f);
		}
		return *this;
	}

	IAllocator& allocator;
	reflscene* scene;
	reflarrayprop* array = nullptr;
	reflprop* last_prop = nullptr;
};

builder build_scene(const char* scene_name);

} // namespace Reflection


} // namespace Lumix
