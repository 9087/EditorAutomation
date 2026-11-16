// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;
class FJsonValue;

namespace EditorAutomation
{
/**
 * Converts property values to nested JSON.
 *
 * Encoding conventions (consumers use the property's Type/schema to interpret strings):
 * - scalars are JSON strings: bool "true"/"false", integers decimal, floats canonical text
 *   ("NaN"/"Infinity"/"-Infinity"), enum numeric entry values;
 * - struct: JSON object keyed by member name; array/set: JSON array;
 * - map: JSON array of {"Key": ..., "Value": ...} objects;
 * - object/class/soft/weak/interface references: object path string, or null when unset.
 */
class FValueJson
{
public:
	/** Build the nested JSON value of one property. */
	static bool ToJson(FProperty* Property, void* ValueAddress, TSharedPtr<FJsonValue>& OutValue, FString& OutError);

	/** Build and serialize the nested JSON text of one property. */
	static bool ToJsonString(FProperty* Property, void* ValueAddress, FString& OutJsonText, FString& OutError);

	/** Write one nested JSON value into a property. */
	static bool FromJson(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* ValueAddress, FString& OutError);

	/** Parse JSON text and write it into a property. */
	static bool FromJsonString(const FString& JsonText, FProperty* Property, void* ValueAddress, FString& OutError);
};
}
