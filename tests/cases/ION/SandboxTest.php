<?php

namespace ION;


use ION\Test\TestCase;

/**
 * Experiments with Zend Engine
 * In prod. all tests should be commented.
 * @package cases\ION
 */
class SandboxTest extends TestCase {

    /**
     * @group _dev
     * @_memcheck
     */
    public function testSandbox() {
//        \ION\Debug::sandbox();
    }
}