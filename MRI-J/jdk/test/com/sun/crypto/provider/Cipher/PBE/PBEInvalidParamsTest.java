/*
 * Copyright 2005-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

/*
 * @test
 * @bug 6209660
 * @summary Ensure that InvalidAlgorithmParameterException is
 * thrown as javadoc specified when parameters of the wrong
 * type are used.
 * @author Valerie Peng
 */
import java.security.*;
import java.security.spec.*;
import javax.crypto.*;
import javax.crypto.spec.*;

public class PBEInvalidParamsTest {

    private static final char[] PASSWORD = { 'p', 'a', 's', 's' };
    private static final String[] PBE_ALGOS = {
        "PBEWithMD5AndDES", "PBEWithSHA1AndDESede", "PBEWithSHA1AndRC2_40"
        // skip "PBEWithMD5AndTripleDES" since it requires Unlimited
        // version of JCE jurisdiction policy files.
    };

    private static final IvParameterSpec INVALID_PARAMS =
        new IvParameterSpec(new byte[8]);

    public static void main(String[] args) throws Exception {
        PBEKeySpec ks = new PBEKeySpec(PASSWORD);
        for (int i = 0; i < PBE_ALGOS.length; i++) {
            String algo = PBE_ALGOS[i];
            System.out.println("=>testing " + algo);
            SecretKeyFactory skf = SecretKeyFactory.getInstance(algo);
            SecretKey key = skf.generateSecret(ks);
            Cipher c = Cipher.getInstance(algo, "SunJCE");
            try {
                c.init(Cipher.ENCRYPT_MODE, key, INVALID_PARAMS);
                throw new Exception("Test Failed: expected IAPE is " +
                                     "not thrown for " + algo);
            } catch (InvalidAlgorithmParameterException iape) {
                continue;
            }
        }
        System.out.println("Test Passed");
    }
}
