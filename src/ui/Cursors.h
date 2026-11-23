/*
 * PowerPaint - a native Haiku port of JS Paint
 * Distributed under the terms of the MIT License.
 *
 * Tool pointers: JS Paint's cursor images, from the app's resources.
 */
#ifndef CURSORS_H
#define CURSORS_H

#include <Cursor.h>


namespace Cursors {

// Never NULL; the cross-hair when the tool's image is missing.
const BCursor*	ForTool(int32 tool);

}	// namespace Cursors

#endif	// CURSORS_H
