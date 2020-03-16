#!/usr/bin/env groovy

pipeline {
    agent none
    stages {
        stage('Build and check') {
            agent {
                dockerfile {
                    filename 'Dockerfile.build'
                }
            }
            stages {
                stage('Lint') {
                    steps {
                        echo 'Linting..'
                        sh 'flake8 -v --format=pylint swig/python/kopano/kopano > pylint.log || true'
                        recordIssues tool: pyLint(pattern: 'pylint.log'), qualityGates: [[threshold: 5000, type: 'TOTAL', unstable: true]]
                    }
                }
                stage('Build') {
                    steps {
                        echo 'Building..'
                        sh './bootstrap.sh'
                        sh 'PYTHON=/usr/bin/python3 ./configure'
                        sh 'make -j $(nproc)'
			recordIssues(tools: [gcc()])
                    }
                }
                stage('Check') {
                    steps {
                        echo 'Checking..'
                        sh 'make check'
                    }
                }
		/*
                stage('PHP MAPI Test') {
                    steps {
                        echo 'Testing php-ext..'
                        sh 'make -C php-ext test TEST_PHP_JUNIT=test.log || true'
                        junit allowEmptyResults: true, testResults: 'php-ext/test.log'
                    }
                }
                stage('Python MAPI Test') {
                    steps {
                        echo 'Testing python-mapi..'
                        sh 'make -C swig/python test PYTEST=pytest-3 || true'
                        junit allowEmptyResults: true, healthScaleFactor: 0.0, testResults: 'swig/python/test.xml'
                    }
                }
                stage('Python Kopano Test') {
                    steps {
                        echo 'Testing python-kopano..'
                        sh 'make -C swig/python/kopano test PYTEST=pytest-3 || true'
                        junit allowEmptyResults: true, healthScaleFactor: 0.0, testResults: 'swig/python/kopano/test.xml'
                    }
                }*/
            }
            post {
                success {
                        stash includes: '**', name: 'workspace'
                }
            }
        }
        stage('Test Suite') {
            agent {
                label 'master'
            }
            stages {
                stage('Verify') {
                    steps {
                        echo 'Checking build...'
                        unstash 'workspace'
                        sh 'ls -lah .libs'
                    }
                }
                stage('Run Test Suite') {
                    steps {
                        echo 'Testing..'
                        sh 'make -C test test-backend-kopano-ci-run EXTRA_LOCAL_ADMIN_USER=$(id -u) DOCKERCOMPOSE_UP_ARGS=--build DOCKERCOMPOSE_EXEC_ARGS="-T -u $(id -u) -e HOME=/workspace" || true'
			junit testResults: 'php-ext/test.log'
                        junit testResults: 'swig/python/test.xml'
                        junit testResults: 'swig/python/kopano/test.xml'
                    }
                }
            }
            post {
                always {
                    sh 'make -C test test-backend-kopano-ci-clean'
                }
            }
        }
    }
}
