#pragma once

#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeFixedString.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypeNullable.h>
#include <Columns/ColumnVector.h>
#include <Columns/ColumnString.h>
#include <Columns/ColumnConst.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnFixedString.h>
#include <Columns/ColumnTuple.h>
#include <Columns/ColumnNullable.h>
#include <Common/typeid_cast.h>
#include <Functions/IFunction.h>
#include <Functions/FunctionHelpers.h>
#include <DataTypes/NumberTraits.h>
#include <DataTypes/getLeastCommonType.h>
#include <Functions/GatherUtils.h>


namespace DB
{

/** Selection function by condition: if(cond, then, else).
  * cond - UInt8
  * then, else - numeric types for which there is a general type, or dates, datetimes, or strings, or arrays of these types.
  */


template <typename A, typename B, typename ResultType>
struct NumIfImpl
{
private:
    static PaddedPODArray<ResultType> & result_vector(Block & block, size_t result, size_t size)
    {
        auto col_res = std::make_shared<ColumnVector<ResultType>>();
        block.getByPosition(result).column = col_res;

        typename ColumnVector<ResultType>::Container_t & vec_res = col_res->getData();
        vec_res.resize(size);

        return vec_res;
    }
public:
    static void vector_vector(
        const PaddedPODArray<UInt8> & cond,
        const PaddedPODArray<A> & a, const PaddedPODArray<B> & b,
        Block & block,
        size_t result)
    {
        size_t size = cond.size();
        PaddedPODArray<ResultType> & res = result_vector(block, result, size);
        for (size_t i = 0; i < size; ++i)
            res[i] = cond[i] ? static_cast<ResultType>(a[i]) : static_cast<ResultType>(b[i]);
    }

    static void vector_constant(
        const PaddedPODArray<UInt8> & cond,
        const PaddedPODArray<A> & a, B b,
        Block & block,
        size_t result)
    {
        size_t size = cond.size();
        PaddedPODArray<ResultType> & res = result_vector(block, result, size);
        for (size_t i = 0; i < size; ++i)
            res[i] = cond[i] ? static_cast<ResultType>(a[i]) : static_cast<ResultType>(b);
    }

    static void constant_vector(
        const PaddedPODArray<UInt8> & cond,
        A a, const PaddedPODArray<B> & b,
        Block & block,
        size_t result)
    {
        size_t size = cond.size();
        PaddedPODArray<ResultType> & res = result_vector(block, result, size);
        for (size_t i = 0; i < size; ++i)
            res[i] = cond[i] ? static_cast<ResultType>(a) : static_cast<ResultType>(b[i]);
    }

    static void constant_constant(
        const PaddedPODArray<UInt8> & cond,
        A a, B b,
        Block & block,
        size_t result)
    {
        size_t size = cond.size();
        PaddedPODArray<ResultType> & res = result_vector(block, result, size);
        for (size_t i = 0; i < size; ++i)
            res[i] = cond[i] ? static_cast<ResultType>(a) : static_cast<ResultType>(b);
    }
};

template <typename A, typename B>
struct NumIfImpl<A, B, NumberTraits::Error>
{
private:
    static void throw_error()
    {
        throw Exception("Internal logic error: invalid types of arguments 2 and 3 of if", ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);
    }
public:
    template <typename... Args> static void vector_vector(Args &&...) { throw_error(); }
    template <typename... Args> static void vector_constant(Args &&...) { throw_error(); }
    template <typename... Args> static void constant_vector(Args &&...) { throw_error(); }
    template <typename... Args> static void constant_constant(Args &&...) { throw_error(); }
};


class FunctionIf : public IFunction
{
public:
    static constexpr auto name = "if";
    static FunctionPtr create(const Context &) { return std::make_shared<FunctionIf>(); }

private:
    template <typename T0, typename T1>
    bool executeRightType(
        const ColumnUInt8 * cond_col,
        Block & block,
        const ColumnNumbers & arguments,
        size_t result,
        const ColumnVector<T0> * col_left)
    {
        const ColumnVector<T1> * col_right_vec = checkAndGetColumn<ColumnVector<T1>>(block.getByPosition(arguments[2]).column.get());
        const ColumnConst * col_right_const = checkAndGetColumnConst<ColumnVector<T1>>(block.getByPosition(arguments[2]).column.get());

        if (!col_right_vec && !col_right_const)
            return false;

        using ResultType = typename NumberTraits::ResultOfIf<T0, T1>::Type;

        if (col_right_vec)
            NumIfImpl<T0, T1, ResultType>::vector_vector(cond_col->getData(), col_left->getData(), col_right_vec->getData(), block, result);
        else
            NumIfImpl<T0, T1, ResultType>::vector_constant(cond_col->getData(), col_left->getData(), col_right_const->template getValue<T1>(), block, result);

        return true;
    }

    template <typename T0, typename T1>
    bool executeConstRightType(
        const ColumnUInt8 * cond_col,
        Block & block,
        const ColumnNumbers & arguments,
        size_t result,
        const ColumnConst * col_left)
    {
        const ColumnVector<T1> * col_right_vec = checkAndGetColumn<ColumnVector<T1>>(block.getByPosition(arguments[2]).column.get());
        const ColumnConst * col_right_const = checkAndGetColumnConst<ColumnVector<T1>>(block.getByPosition(arguments[2]).column.get());

        if (!col_right_vec && !col_right_const)
            return false;

        using ResultType = typename NumberTraits::ResultOfIf<T0, T1>::Type;

        if (col_right_vec)
            NumIfImpl<T0, T1, ResultType>::constant_vector(cond_col->getData(), col_left->template getValue<T0>(), col_right_vec->getData(), block, result);
        else
            NumIfImpl<T0, T1, ResultType>::constant_constant(cond_col->getData(), col_left->template getValue<T0>(), col_right_const->template getValue<T1>(), block, result);

        return true;
    }

    template <typename T0, typename T1>
    bool executeRightTypeArray(
        [[maybe_unused]] const ColumnUInt8 * cond_col,
        [[maybe_unused]] Block & block,
        [[maybe_unused]] const ColumnNumbers & arguments,
        [[maybe_unused]] size_t result,
        [[maybe_unused]] const ColumnArray * col_left_array)
    {
        if constexpr (std::is_same<NumberTraits::Error, typename NumberTraits::ResultOfIf<T0, T1>::Type>::value)
            return false;
        else
        {
            const IColumn * col_right_untyped = block.getByPosition(arguments[2]).column.get();

            const ColumnArray * col_right_array = checkAndGetColumn<ColumnArray>(col_right_untyped);
            const ColumnConst * col_right_const_array = checkAndGetColumnConst<ColumnArray>(col_right_untyped);

            if (!col_right_array && !col_right_const_array)
                return false;

            using ResultType = typename NumberTraits::ResultOfIf<T0, T1>::Type;

            if (col_right_array)
            {
                const ColumnVector<T1> * col_right_vec = checkAndGetColumn<ColumnVector<T1>>(&col_right_array->getData());

                if (!col_right_vec)
                    return false;

                block.getByPosition(result).column = block.getByPosition(result).type->createColumn();

                conditional(
                    NumericArraySource<T0>(*col_left_array),
                    NumericArraySource<T1>(*col_right_array),
                    NumericArraySink<ResultType>(static_cast<ColumnArray &>(*block.getByPosition(result).column), block.rows()),
                    cond_col->getData());
            }
            else
            {
                const ColumnArray * col_right_const_array_data = checkAndGetColumn<ColumnArray>(&col_right_const_array->getDataColumn());
                if (!checkColumn<ColumnVector<T1>>(&col_right_const_array_data->getData()))
                    return false;

                block.getByPosition(result).column = block.getByPosition(result).type->createColumn();

                conditional(
                    NumericArraySource<T0>(*col_left_array),
                    ConstSource<NumericArraySource<T1>>(*col_right_const_array),
                    NumericArraySink<ResultType>(static_cast<ColumnArray &>(*block.getByPosition(result).column), block.rows()),
                    cond_col->getData());
            }

            return true;
        }
    }

    template <typename T0, typename T1>
    bool executeConstRightTypeArray(
        [[maybe_unused]] const ColumnUInt8 * cond_col,
        [[maybe_unused]] Block & block,
        [[maybe_unused]] const ColumnNumbers & arguments,
        [[maybe_unused]] size_t result,
        [[maybe_unused]] const ColumnConst * col_left_const_array)
    {
        if constexpr (std::is_same<NumberTraits::Error, typename NumberTraits::ResultOfIf<T0, T1>::Type>::value)
            return false;
        else
        {
            const IColumn * col_right_untyped = block.getByPosition(arguments[2]).column.get();

            const ColumnArray * col_right_array = checkAndGetColumn<ColumnArray>(col_right_untyped);
            const ColumnConst * col_right_const_array = checkAndGetColumnConst<ColumnArray>(col_right_untyped);

            if (!col_right_array && !col_right_const_array)
                return false;

            using ResultType = typename NumberTraits::ResultOfIf<T0, T1>::Type;

            if (col_right_array)
            {
                const ColumnVector<T1> * col_right_vec = checkAndGetColumn<ColumnVector<T1>>(&col_right_array->getData());

                if (!col_right_vec)
                    return false;

                block.getByPosition(result).column = block.getByPosition(result).type->createColumn();

                conditional(
                    ConstSource<NumericArraySource<T0>>(*col_left_const_array),
                    NumericArraySource<T1>(*col_right_array),
                    NumericArraySink<ResultType>(static_cast<ColumnArray &>(*block.getByPosition(result).column), block.rows()),
                    cond_col->getData());
            }
            else
            {
                const ColumnArray * col_right_const_array_data = checkAndGetColumn<ColumnArray>(&col_right_const_array->getDataColumn());
                if (!checkColumn<ColumnVector<T1>>(&col_right_const_array_data->getData()))
                    return false;

                block.getByPosition(result).column = block.getByPosition(result).type->createColumn();

                conditional(
                    ConstSource<NumericArraySource<T0>>(*col_left_const_array),
                    ConstSource<NumericArraySource<T1>>(*col_right_const_array),
                    NumericArraySink<ResultType>(static_cast<ColumnArray &>(*block.getByPosition(result).column), block.rows()),
                    cond_col->getData());
            }

            return true;
        }
    }

    template <typename T0>
    bool executeLeftType(const ColumnUInt8 * cond_col, Block & block, const ColumnNumbers & arguments, size_t result)
    {
        const IColumn * col_left_untyped = block.getByPosition(arguments[1]).column.get();

        const ColumnVector<T0> * col_left = nullptr;
        const ColumnConst * col_const_left = nullptr;
        const ColumnArray * col_arr_left = nullptr;
        const ColumnVector<T0> * col_arr_left_elems = nullptr;
        const ColumnConst * col_const_arr_left = nullptr;

        col_left = checkAndGetColumn<ColumnVector<T0>>(col_left_untyped);
        if (!col_left)
        {
            col_const_left = checkAndGetColumnConst<ColumnVector<T0>>(col_left_untyped);
            if (!col_const_left)
            {
                col_arr_left = checkAndGetColumn<ColumnArray>(col_left_untyped);

                if (col_arr_left)
                    col_arr_left_elems = checkAndGetColumn<ColumnVector<T0>>(&col_arr_left->getData());
                else
                    col_const_arr_left = checkAndGetColumnConst<ColumnArray>(col_left_untyped);
            }
        }

        if (col_left)
        {
            if (   executeRightType<T0, UInt8>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, UInt16>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, UInt32>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, UInt64>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, Int8>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, Int16>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, Int32>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, Int64>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, Float32>(cond_col, block, arguments, result, col_left)
                || executeRightType<T0, Float64>(cond_col, block, arguments, result, col_left))
                return true;
            else
                throw Exception("Illegal column " + block.getByPosition(arguments[2]).column->getName()
                    + " of third argument of function " + getName(),
                    ErrorCodes::ILLEGAL_COLUMN);
        }
        else if (col_const_left)
        {
            if (   executeConstRightType<T0, UInt8>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, UInt16>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, UInt32>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, UInt64>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, Int8>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, Int16>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, Int32>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, Int64>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, Float32>(cond_col, block, arguments, result, col_const_left)
                || executeConstRightType<T0, Float64>(cond_col, block, arguments, result, col_const_left))
                return true;
            else
                throw Exception("Illegal column " + block.getByPosition(arguments[2]).column->getName()
                    + " of third argument of function " + getName(),
                    ErrorCodes::ILLEGAL_COLUMN);
        }
        else if (col_arr_left && col_arr_left_elems)
        {
            if (   executeRightTypeArray<T0, UInt8>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, UInt16>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, UInt32>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, UInt64>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, Int8>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, Int16>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, Int32>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, Int64>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, Float32>(cond_col, block, arguments, result, col_arr_left)
                || executeRightTypeArray<T0, Float64>(cond_col, block, arguments, result, col_arr_left))
                return true;
            else
                throw Exception("Illegal column " + block.getByPosition(arguments[2]).column->getName()
                    + " of third argument of function " + getName(),
                    ErrorCodes::ILLEGAL_COLUMN);
        }
        else if (col_const_arr_left && checkColumn<ColumnVector<T0>>(&static_cast<const ColumnArray &>(col_const_arr_left->getDataColumn()).getData()))
        {
            if (   executeConstRightTypeArray<T0, UInt8>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, UInt16>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, UInt32>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, UInt64>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, Int8>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, Int16>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, Int32>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, Int64>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, Float32>(cond_col, block, arguments, result, col_const_arr_left)
                || executeConstRightTypeArray<T0, Float64>(cond_col, block, arguments, result, col_const_arr_left))
                return true;
            else
                throw Exception("Illegal column " + block.getByPosition(arguments[2]).column->getName()
                    + " of third argument of function " + getName(),
                    ErrorCodes::ILLEGAL_COLUMN);
        }

        return false;
    }

    bool executeString(const ColumnUInt8 * cond_col, Block & block, const ColumnNumbers & arguments, size_t result)
    {
        const IColumn * col_then_untyped = block.getByPosition(arguments[1]).column.get();
        const IColumn * col_else_untyped = block.getByPosition(arguments[2]).column.get();

        const ColumnString * col_then = checkAndGetColumn<ColumnString>(col_then_untyped);
        const ColumnString * col_else = checkAndGetColumn<ColumnString>(col_else_untyped);
        const ColumnFixedString * col_then_fixed = checkAndGetColumn<ColumnFixedString>(col_then_untyped);
        const ColumnFixedString * col_else_fixed = checkAndGetColumn<ColumnFixedString>(col_else_untyped);
        const ColumnConst * col_then_const = checkAndGetColumnConst<ColumnString>(col_then_untyped);
        const ColumnConst * col_else_const = checkAndGetColumnConst<ColumnString>(col_else_untyped);
        const ColumnConst * col_then_const_fixed = checkAndGetColumnConst<ColumnFixedString>(col_then_untyped);
        const ColumnConst * col_else_const_fixed = checkAndGetColumnConst<ColumnFixedString>(col_else_untyped);

        const PaddedPODArray<UInt8> & cond_data = cond_col->getData();
        size_t rows = cond_data.size();

        if ((col_then_fixed || col_then_const_fixed)
            && (col_else_fixed || col_else_const_fixed))
        {
            /// The result is FixedString.

            auto col_res_untyped = block.getByPosition(result).type->createColumn();
            block.getByPosition(result).column = col_res_untyped;
            ColumnFixedString * col_res = static_cast<ColumnFixedString *>(col_res_untyped.get());
            auto sink = FixedStringSink(*col_res, rows);

            if (col_then_fixed && col_else_fixed)
                conditional(FixedStringSource(*col_then_fixed), FixedStringSource(*col_else_fixed), sink, cond_data);
            else if (col_then_fixed && col_else_const_fixed)
                conditional(FixedStringSource(*col_then_fixed), ConstSource<FixedStringSource>(*col_else_const_fixed), sink, cond_data);
            else if (col_then_const_fixed && col_else_fixed)
                conditional(ConstSource<FixedStringSource>(*col_then_const_fixed), FixedStringSource(*col_else_fixed), sink, cond_data);
            else if (col_then_const_fixed && col_else_const_fixed)
                conditional(ConstSource<FixedStringSource>(*col_then_const_fixed), ConstSource<FixedStringSource>(*col_else_const_fixed), sink, cond_data);

            return true;
        }

        if ((col_then || col_then_const || col_then_fixed || col_then_const_fixed)
            && (col_else || col_else_const || col_else_fixed || col_else_const_fixed))
        {
            /// The result is String.
            std::shared_ptr<ColumnString> col_res = std::make_shared<ColumnString>();
            block.getByPosition(result).column = col_res;
            auto sink = StringSink(*col_res, rows);

            if (col_then && col_else)
                conditional(StringSource(*col_then), StringSource(*col_else), sink, cond_data);
            else if (col_then && col_else_const)
                conditional(StringSource(*col_then), ConstSource<StringSource>(*col_else_const), sink, cond_data);
            else if (col_then_const && col_else)
                conditional(ConstSource<StringSource>(*col_then_const), StringSource(*col_else), sink, cond_data);
            else if (col_then_const && col_else_const)
                conditional(ConstSource<StringSource>(*col_then_const), ConstSource<StringSource>(*col_else_const), sink, cond_data);
            else if (col_then && col_else_fixed)
                conditional(StringSource(*col_then), FixedStringSource(*col_else_fixed), sink, cond_data);
            else if (col_then_fixed && col_else)
                conditional(FixedStringSource(*col_then_fixed), StringSource(*col_else), sink, cond_data);
            else if (col_then_const && col_else_fixed)
                conditional(ConstSource<StringSource>(*col_then_const), FixedStringSource(*col_else_fixed), sink, cond_data);
            else if (col_then_fixed && col_else_const)
                conditional(FixedStringSource(*col_then_fixed), ConstSource<StringSource>(*col_else_const), sink, cond_data);
            else if (col_then && col_else_const_fixed)
                conditional(StringSource(*col_then), ConstSource<FixedStringSource>(*col_else_const_fixed), sink, cond_data);
            else if (col_then_const_fixed && col_else)
                conditional(ConstSource<FixedStringSource>(*col_then_const_fixed), StringSource(*col_else), sink, cond_data);
            else if (col_then_const && col_else_const_fixed)
                conditional(ConstSource<StringSource>(*col_then_const), ConstSource<FixedStringSource>(*col_else_const_fixed), sink, cond_data);
            else if (col_then_const_fixed && col_else_const)
                conditional(ConstSource<FixedStringSource>(*col_then_const_fixed), ConstSource<StringSource>(*col_else_const), sink, cond_data);

            return true;
        }

        return false;
    }

    bool executeGenericArray(const ColumnUInt8 * cond_col, Block & block, const ColumnNumbers & arguments, size_t result)
    {
        /// For generic implementation, arrays must be of same type.
        if (!block.getByPosition(arguments[1]).type->equals(*block.getByPosition(arguments[2]).type))
            return false;

        const IColumn * col_then_untyped = block.getByPosition(arguments[1]).column.get();
        const IColumn * col_else_untyped = block.getByPosition(arguments[2]).column.get();

        const ColumnArray * col_arr_then = checkAndGetColumn<ColumnArray>(col_then_untyped);
        const ColumnArray * col_arr_else = checkAndGetColumn<ColumnArray>(col_else_untyped);
        const ColumnConst * col_arr_then_const = checkAndGetColumnConst<ColumnArray>(col_then_untyped);
        const ColumnConst * col_arr_else_const = checkAndGetColumnConst<ColumnArray>(col_else_untyped);

        const PaddedPODArray<UInt8> & cond_data = cond_col->getData();
        size_t rows = cond_data.size();

        if ((col_arr_then || col_arr_then_const)
            && (col_arr_else || col_arr_else_const))
        {
            block.getByPosition(result).column = block.getByPosition(result).type->createColumn();
            auto col_res = static_cast<ColumnArray *>(block.getByPosition(result).column.get());

            if (col_arr_then && col_arr_else)
                conditional(GenericArraySource(*col_arr_then), GenericArraySource(*col_arr_else), GenericArraySink(*col_res, rows), cond_data);
            else if (col_arr_then && col_arr_else_const)
                conditional(GenericArraySource(*col_arr_then), ConstSource<GenericArraySource>(*col_arr_else_const), GenericArraySink(*col_res, rows), cond_data);
            else if (col_arr_then_const && col_arr_else)
                conditional(ConstSource<GenericArraySource>(*col_arr_then_const), GenericArraySource(*col_arr_else), GenericArraySink(*col_res, rows), cond_data);
            else if (col_arr_then_const && col_arr_else_const)
                conditional(ConstSource<GenericArraySource>(*col_arr_then_const), ConstSource<GenericArraySource>(*col_arr_else_const), GenericArraySink(*col_res, rows), cond_data);
            else
                return false;

            return true;
        }

        return false;
    }

    bool executeTuple(Block & block, const ColumnNumbers & arguments, size_t result)
    {
        /// Calculate function for each corresponding elements of tuples.

        const ColumnWithTypeAndName & arg1 = block.getByPosition(arguments[1]);
        const ColumnWithTypeAndName & arg2 = block.getByPosition(arguments[2]);

        Columns col1_contents;
        Columns col2_contents;

        if (const ColumnTuple * tuple1 = typeid_cast<const ColumnTuple *>(arg1.column.get()))
            col1_contents = tuple1->getColumns();
        else if (const ColumnConst * const_tuple = checkAndGetColumnConst<ColumnTuple>(arg1.column.get()))
            col1_contents = convertConstTupleToConstantElements(*const_tuple);
        else
            return false;

        if (const ColumnTuple * tuple2 = typeid_cast<const ColumnTuple *>(arg2.column.get()))
            col2_contents = tuple2->getColumns();
        else if (const ColumnConst * const_tuple = checkAndGetColumnConst<ColumnTuple>(arg2.column.get()))
            col2_contents = convertConstTupleToConstantElements(*const_tuple);
        else
            return false;

        const DataTypeTuple & type1 = static_cast<const DataTypeTuple &>(*arg1.type);
        const DataTypeTuple & type2 = static_cast<const DataTypeTuple &>(*arg2.type);

        Block temporary_block;
        temporary_block.insert(block.getByPosition(arguments[0]));

        size_t tuple_size = type1.getElements().size();
        Columns tuple_columns(tuple_size);

        for (size_t i = 0; i < tuple_size; ++i)
        {
            temporary_block.insert({nullptr,
                getReturnTypeImpl({std::make_shared<DataTypeUInt8>(), type1.getElements()[i], type2.getElements()[i]}),
                {}});

            temporary_block.insert({col1_contents[i], type1.getElements()[i], {}});
            temporary_block.insert({col2_contents[i], type2.getElements()[i], {}});

            /// temporary_block will be: cond, res_0, ..., res_i, then_i, else_i
            executeImpl(temporary_block, {0, i + 2, i + 3}, i + 1);
            temporary_block.erase(i + 3);
            temporary_block.erase(i + 2);

            tuple_columns[i] = temporary_block.getByPosition(i + 1).column;
        }

        /// temporary_block is: cond, res_0, res_1, res_2...

        block.getByPosition(result).column = std::make_shared<ColumnTuple>(tuple_columns);
        return true;
    }

    bool executeForNullableCondition(Block & block, const ColumnNumbers & arguments, size_t result)
    {
        const ColumnWithTypeAndName & arg_cond = block.getByPosition(arguments[0]);
        bool cond_is_null = arg_cond.column->onlyNull();
        bool cond_is_nullable = arg_cond.column->isColumnNullable();

        if (cond_is_null)
        {
            block.getByPosition(result).column = block.getByPosition(result).type->createColumnConst(block.rows(), Null());
            return true;
        }

        if (cond_is_nullable)
        {
            Block temporary_block
            {
                { static_cast<const ColumnNullable &>(*arg_cond.column).getNestedColumn(), arg_cond.type, arg_cond.name },
                block.getByPosition(arguments[1]),
                block.getByPosition(arguments[2]),
                block.getByPosition(result)
            };

            executeImpl(temporary_block, {0, 1, 2}, 3);

            ColumnPtr & result_column = block.getByPosition(result).column;
            result_column = temporary_block.getByPosition(3).column;

            if (ColumnNullable * result_nullable = typeid_cast<ColumnNullable *>(result_column.get()))
            {
                result_nullable->applyNullMap(static_cast<const ColumnNullable &>(*arg_cond.column));
            }
            else if (result_column->onlyNull())
            {
                result_column = block.getByPosition(result).type->createColumnConst(block.rows(), Null());
            }
            else
            {
                result_column = std::make_shared<ColumnNullable>(
                    materializeColumnIfConst(result_column), static_cast<const ColumnNullable &>(*arg_cond.column).getNullMapColumn());
            }

            return true;
        }

        return false;
    }

    static const ColumnPtr materializeColumnIfConst(const ColumnPtr & column)
    {
        if (auto res = column->convertToFullColumnIfConst())
            return res;
        return column;
    }

    static const ColumnPtr makeNullableColumnIfNot(const ColumnPtr & column)
    {
        if (column->isColumnNullable())
            return column;

        return std::make_shared<ColumnNullable>(
            materializeColumnIfConst(column), std::make_shared<ColumnUInt8>(column->size(), 0));
    }

    static const ColumnPtr getNestedColumn(const ColumnPtr & column)
    {
        if (column->isColumnNullable())
            return static_cast<const ColumnNullable &>(*column).getNestedColumn();

        return column;
    }

    bool executeForNullableThenElse(Block & block, const ColumnNumbers & arguments, size_t result)
    {
        const ColumnWithTypeAndName & arg_cond = block.getByPosition(arguments[0]);
        const ColumnWithTypeAndName & arg_then = block.getByPosition(arguments[1]);
        const ColumnWithTypeAndName & arg_else = block.getByPosition(arguments[2]);

        bool then_is_nullable = typeid_cast<const ColumnNullable *>(arg_then.column.get());
        bool else_is_nullable = typeid_cast<const ColumnNullable *>(arg_else.column.get());

        if (!then_is_nullable && !else_is_nullable)
            return false;

        /** Calculate null mask of result and nested column separately.
          */
        ColumnPtr result_null_mask;

        {
            Block temporary_block(
            {
                arg_cond,
                {
                    then_is_nullable
                        ? static_cast<const ColumnNullable *>(arg_then.column.get())->getNullMapColumn()
                        : DataTypeUInt8().createColumnConst(block.rows(), UInt64(0)),
                    std::make_shared<DataTypeUInt8>(),
                    ""
                },
                {
                    else_is_nullable
                        ? static_cast<const ColumnNullable *>(arg_else.column.get())->getNullMapColumn()
                        : DataTypeUInt8().createColumnConst(block.rows(), UInt64(0)),
                    std::make_shared<DataTypeUInt8>(),
                    ""
                },
                {
                    nullptr,
                    std::make_shared<DataTypeUInt8>(),
                    ""
                }
            });

            executeImpl(temporary_block, {0, 1, 2}, 3);

            result_null_mask = temporary_block.getByPosition(3).column;
        }

        ColumnPtr result_nested_column;

        {
            Block temporary_block(
            {
                arg_cond,
                {
                    getNestedColumn(arg_then.column),
                    removeNullable(arg_then.type),
                    ""
                },
                {
                    getNestedColumn(arg_else.column),
                    removeNullable(arg_else.type),
                    ""
                },
                {
                    nullptr,
                    removeNullable(block.getByPosition(result).type),
                    ""
                }
            });

            executeImpl(temporary_block, {0, 1, 2}, 3);

            result_nested_column = temporary_block.getByPosition(3).column;
        }

        block.getByPosition(result).column = std::make_shared<ColumnNullable>(
            materializeColumnIfConst(result_nested_column), materializeColumnIfConst(result_null_mask));
        return true;
    }

    bool executeForNullThenElse(Block & block, const ColumnNumbers & arguments, size_t result)
    {
        const ColumnWithTypeAndName & arg_cond = block.getByPosition(arguments[0]);
        const ColumnWithTypeAndName & arg_then = block.getByPosition(arguments[1]);
        const ColumnWithTypeAndName & arg_else = block.getByPosition(arguments[2]);

        bool then_is_null = arg_then.column->onlyNull();
        bool else_is_null = arg_else.column->onlyNull();

        if (!then_is_null && !else_is_null)
            return false;

        if (then_is_null && else_is_null)
        {
            block.getByPosition(result).column = block.getByPosition(result).type->createColumnConst(block.rows(), Null());
            return true;
        }

        const ColumnUInt8 * cond_col = typeid_cast<const ColumnUInt8 *>(arg_cond.column.get());
        const ColumnConst * cond_const_col = checkAndGetColumnConst<ColumnVector<UInt8>>(arg_cond.column.get());

        /// If then is NULL, we create Nullable column with null mask OR-ed with condition.
        if (then_is_null)
        {
            if (cond_col)
            {
                if (arg_else.column->isColumnNullable())
                {
                    auto result_column = arg_else.column->clone();
                    static_cast<ColumnNullable &>(*result_column).applyNullMap(static_cast<const ColumnUInt8 &>(*arg_cond.column));
                    block.getByPosition(result).column = result_column;
                }
                else
                {
                    block.getByPosition(result).column = std::make_shared<ColumnNullable>(
                        materializeColumnIfConst(arg_else.column), arg_cond.column->clone());
                }
            }
            else if (cond_const_col)
            {
                block.getByPosition(result).column = cond_const_col->getValue<UInt8>()
                    ? block.getByPosition(result).type->createColumn()->cloneResized(block.rows())
                    : makeNullableColumnIfNot(arg_else.column);
            }
            else
                throw Exception("Illegal column " + arg_cond.column->getName() + " of first argument of function " + getName()
                    + ". Must be ColumnUInt8 or ColumnConstUInt8.",
                    ErrorCodes::ILLEGAL_COLUMN);
            return true;
        }

        /// If else is NULL, we create Nullable column with null mask OR-ed with negated condition.
        if (else_is_null)
        {
            if (cond_col)
            {
                size_t size = block.rows();
                auto & null_map_data = cond_col->getData();

                auto negated_null_map = std::make_shared<ColumnUInt8>();
                auto & negated_null_map_data = negated_null_map->getData();
                negated_null_map_data.resize(size);

                for (size_t i = 0; i < size; ++i)
                    negated_null_map_data[i] = !null_map_data[i];

                if (arg_then.column->isColumnNullable())
                {
                    auto result_column = arg_then.column->clone();
                    static_cast<ColumnNullable &>(*result_column).applyNegatedNullMap(static_cast<const ColumnUInt8 &>(*arg_cond.column));
                    block.getByPosition(result).column = result_column;
                }
                else
                {
                    block.getByPosition(result).column = std::make_shared<ColumnNullable>(
                        materializeColumnIfConst(arg_then.column), negated_null_map);
                }
            }
            else if (cond_const_col)
            {
                block.getByPosition(result).column = cond_const_col->getValue<UInt8>()
                    ? makeNullableColumnIfNot(arg_then.column)
                    : block.getByPosition(result).type->createColumn()->cloneResized(block.rows());
            }
            else
                throw Exception("Illegal column " + arg_cond.column->getName() + " of first argument of function " + getName()
                    + ". Must be ColumnUInt8 or ColumnConstUInt8.",
                    ErrorCodes::ILLEGAL_COLUMN);
            return true;
        }

        return false;
    }

public:
    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override { return 3; }

    bool useDefaultImplementationForNulls() const override { return false; }

    /// Get result types by argument types. If the function does not apply to these arguments, throw an exception.
    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments[0]->onlyNull())
            return arguments[0];

        if (arguments[0]->isNullable())
            return makeNullable(getReturnTypeImpl({
                removeNullable(arguments[0]), arguments[1], arguments[2]}));

        if (!checkDataType<DataTypeUInt8>(arguments[0].get()))
            throw Exception("Illegal type " + arguments[0]->getName() + " of first argument (condition) of function if. Must be UInt8.",
                ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT);

        return getLeastCommonType({arguments[1], arguments[2]});
    }

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        if (executeForNullableCondition(block, arguments, result)
            || executeForNullThenElse(block, arguments, result)
            || executeForNullableThenElse(block, arguments, result))
            return;

        const ColumnWithTypeAndName & arg_cond = block.getByPosition(arguments[0]);
        const ColumnWithTypeAndName & arg_then = block.getByPosition(arguments[1]);
        const ColumnWithTypeAndName & arg_else = block.getByPosition(arguments[2]);

        const ColumnUInt8 * cond_col = typeid_cast<const ColumnUInt8 *>(arg_cond.column.get());
        const ColumnConst * cond_const_col = checkAndGetColumnConst<ColumnVector<UInt8>>(arg_cond.column.get());
        ColumnPtr materialized_cond_col;

        if (cond_const_col)
        {
            if (arg_then.type->equals(*arg_else.type))
            {
                block.getByPosition(result).column = cond_const_col->getValue<UInt8>()
                    ? arg_then.column
                    : arg_else.column;
                return;
            }
            else
            {
                materialized_cond_col = cond_const_col->convertToFullColumn();
                cond_col = typeid_cast<const ColumnUInt8 *>(&*materialized_cond_col);
            }
        }

        if (cond_col)
        {
            if (!( executeLeftType<UInt8>(cond_col, block, arguments, result)
                || executeLeftType<UInt16>(cond_col, block, arguments, result)
                || executeLeftType<UInt32>(cond_col, block, arguments, result)
                || executeLeftType<UInt64>(cond_col, block, arguments, result)
                || executeLeftType<Int8>(cond_col, block, arguments, result)
                || executeLeftType<Int16>(cond_col, block, arguments, result)
                || executeLeftType<Int32>(cond_col, block, arguments, result)
                || executeLeftType<Int64>(cond_col, block, arguments, result)
                || executeLeftType<Float32>(cond_col, block, arguments, result)
                || executeLeftType<Float64>(cond_col, block, arguments, result)
                || executeString(cond_col, block, arguments, result)
                || executeGenericArray(cond_col, block, arguments, result)
                || executeTuple(block, arguments, result)))
                throw Exception("Illegal columns " + arg_then.column->getName()
                    + " and " + arg_else.column->getName()
                    + " of second (then) and third (else) arguments of function " + getName(),
                    ErrorCodes::ILLEGAL_COLUMN);
        }
        else
            throw Exception("Illegal column " + arg_cond.column->getName() + " of first argument of function " + getName()
                + ". Must be ColumnUInt8 or ColumnConstUInt8.",
                ErrorCodes::ILLEGAL_COLUMN);
    }
};


/// Function multiIf, which generalizes the function if.
///
/// Syntax: multiIf(cond_1, then_1, ..., cond_N, then_N, else)
/// where N >= 1.
///
/// For all 1 <= i <= N, "cond_i" has type UInt8.
/// Types of all the branches "then_i" and "else" are either of the following:
///    - numeric types for which there exists a common type;
///    - dates;
///    - dates with time;
///    - strings;
///    - arrays of such types.
///
/// Additionally the arguments, conditions or branches, support nullable types
/// and the NULL value.
class FunctionMultiIf final : public IFunction
{
public:
    static constexpr auto name = "multiIf";
    static FunctionPtr create(const Context & context);
    FunctionMultiIf(const Context & context) : context(context) {};

public:
    String getName() const override;
    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }
    bool useDefaultImplementationForNulls() const override { return false; }
    DataTypePtr getReturnTypeImpl(const DataTypes & args) const override;
    void executeImpl(Block & block, const ColumnNumbers & args, size_t result) override;

private:
    const Context & context;
};


/// Implements the CASE construction when it is
/// provided an expression. Users should not call this function.
class FunctionCaseWithExpression : public IFunction
{
public:
    static constexpr auto name = "caseWithExpression";
    static FunctionPtr create(const Context & context_);

public:
    FunctionCaseWithExpression(const Context & context_);
    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }
    String getName() const override;
    DataTypePtr getReturnTypeImpl(const DataTypes & args) const override;
    void executeImpl(Block & block, const ColumnNumbers & args, size_t result) override;

private:
    const Context & context;
};

}
