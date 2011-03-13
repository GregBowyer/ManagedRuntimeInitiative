// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License version 2 only, as published by
// the Free Software Foundation.
//
// Azul designates this particular file as subject to the "Classpath" exception
// as provided by Azul in the LICENSE file that accompanied this code.
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2
// along with this work; if not, write to the Free Software Foundation,Inc.,
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View,
// CA 94043 USA, or visit www.azulsystems.com if you need additional information
// or have any questions.
package com.azulsystems.arta;

public class ARTA {
    public static WebServerState getWebServerState() {
        WebServerState state = new WebServerState();
        getWebServerState0(state);
        return state;
    }

    public static Response service(String uri) {
        Response response = new Response();
        service0(uri, response);
        return response;
    }

    private static native void getWebServerState0(WebServerState state);
    private static native void service0(String uri, Response response);
}
